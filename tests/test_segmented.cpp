#include <filesystem>
#include <fstream>

#include <jsonser/jsonser.hpp>

#include "check.hpp"

namespace fs = std::filesystem;

struct Rec {
    std::uint64_t id = 0;
    std::string name;
    std::vector<int> tags;
    std::optional<double> score;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(name) & JSONSER_NVP(tags) & JSONSER_NVP(score);
    }
    bool operator==(const Rec& o) const { return id == o.id && name == o.name && tags == o.tags && score == o.score; }
};
JSONSER_CLASS_VERSION(Rec, 4)

struct Header {
    std::string title;
    std::map<std::string, int> counts;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(title) & JSONSER_NVP(counts);
    }
};

static Rec make_rec(std::uint64_t i) {
    Rec r;
    r.id = i;
    r.name = "record-" + std::to_string(i);
    r.tags = {int(i % 7), int(i % 13)};
    if (i % 3 == 0) r.score = double(i) / 8;
    return r;
}

static fs::path temp_dir(const char* name) {
    fs::path p = fs::temp_directory_path() / "jsonser_tests" / name;
    fs::remove_all(p);
    return p;
}

static std::string slurp(const fs::path& p) {
    std::ifstream is(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
}

constexpr std::uint64_t N = 200000;

static void write_archive(const fs::path& dir, std::uint64_t n = N) {
    jsonser::segmented_writer w(dir, {1u << 20});  // 1 MB segments -> several files
    Header h{"test", {{"recs", int(n)}}};
    w.write("header", h);
    auto s = w.stream<Rec>("recs");
    for (std::uint64_t i = 0; i < n; ++i) s.push(make_rec(i));
    s.close();
    w.close();
}

TEST(round_trip_large_stream) {
    fs::path dir = temp_dir("large");
    write_archive(dir);
    jsonser::segmented_reader r(dir);
    CHECK_EQ(r.info().types.at("Rec"), 4u);
    CHECK_EQ(r.count("recs"), N);
    const auto* e = r.info().find("recs");
    CHECK(e && e->segments.size() > 3);
    CHECK(e && e->type == "Rec");

    Header h = r.read<Header>("header");
    CHECK_EQ(h.title, "test");
    CHECK_EQ(h.counts.at("recs"), int(N));

    std::uint64_t i = 0;
    bool all_equal = true;
    for (Rec& rec : r.stream<Rec>("recs")) {
        if (!(rec == make_rec(i))) all_equal = false;
        ++i;
    }
    CHECK(all_equal);
    CHECK_EQ(i, N);
}

TEST(random_access) {
    fs::path dir = temp_dir("random");
    write_archive(dir);
    jsonser::segmented_reader r(dir);
    for (std::uint64_t i : {std::uint64_t(0), std::uint64_t(1), std::uint64_t(12345), N / 2, N - 1})
        CHECK(r.read_at<Rec>("recs", i) == make_rec(i));
    // every segment boundary
    for (auto& s : r.info().find("recs")->segments) {
        CHECK(r.read_at<Rec>("recs", s.first_index) == make_rec(s.first_index));
        CHECK(r.read_at<Rec>("recs", s.first_index + s.count - 1) == make_rec(s.first_index + s.count - 1));
    }
    jsonser::segmented_reader strict(dir, {true, true});
    CHECK(strict.read_at<Rec>("recs", 777) == make_rec(777));
    CHECK_THROWS_WITH(r.read_at<Rec>("recs", N), "out of range");
    CHECK_THROWS_WITH(r.read_at<Rec>("header", 0), "is a object, not a stream");
    CHECK_THROWS_WITH(r.read_at<Rec>("nope", 0), "no entry 'nope'");
}

TEST(segment_files_are_plain_json_lines) {
    fs::path dir = temp_dir("plain");
    write_archive(dir, 3);
    jsonser::segmented_reader r(dir);
    std::string seg = slurp(dir / r.info().find("recs")->segments[0].file);
    CHECK_EQ(seg,
             "[\n"
             "{\"id\":0,\"name\":\"record-0\",\"tags\":[0,0],\"score\":0},\n"
             "{\"id\":1,\"name\":\"record-1\",\"tags\":[1,1],\"score\":null},\n"
             "{\"id\":2,\"name\":\"record-2\",\"tags\":[2,2],\"score\":null}\n"
             "]\n");
    auto m = jsonser::json::parse(slurp(dir / "manifest.json"));
    CHECK_EQ(m["jsonser"]["layout"], "segmented");
    CHECK_EQ(m["entries"][1]["segments"][0]["count"], 3);
}

TEST(crc_detects_corruption) {
    fs::path dir = temp_dir("corrupt");
    write_archive(dir, 1000);
    jsonser::segmented_reader r(dir);
    fs::path seg = dir / r.info().find("recs")->segments[0].file;
    std::string text = slurp(seg);
    auto pos = text.find("record-500");
    text[pos + 7] = '9';  // record-500 -> record-900: still valid JSON, wrong bytes
    std::ofstream(seg, std::ios::binary | std::ios::trunc) << text;

    auto read_all = [&] {
        for (Rec& rec : r.stream<Rec>("recs")) (void)rec;
    };
    CHECK_THROWS_WITH(read_all(), "CRC-32 mismatch");
    CHECK(r.read_at<Rec>("recs", 500).name == "record-900");  // read_at does not verify by default
    jsonser::segmented_reader strict(dir, {true, true});
    CHECK_THROWS_WITH(strict.read_at<Rec>("recs", 3), "CRC-32 mismatch");
    jsonser::segmented_reader lax(dir, {false, false});
    std::uint64_t n = 0;
    for (Rec& rec : lax.stream<Rec>("recs")) n += 1 + 0 * rec.id;
    CHECK_EQ(n, 1000u);

    fs::path hdr = dir / r.info().find("header")->segments[0].file;
    std::string h = slurp(hdr);
    h[h.find("test")] = 'b';
    std::ofstream(hdr, std::ios::binary | std::ios::trunc) << h;
    CHECK_THROWS_WITH(r.read<Header>("header"), "CRC-32 mismatch");
}

TEST(reformatted_segment_still_reads) {
    fs::path dir = temp_dir("reformatted");
    write_archive(dir, 50);
    jsonser::segmented_reader r(dir, {false, false});  // another tool rewrote it: CRC no longer applies
    fs::path seg = dir / r.info().find("recs")->segments[0].file;
    auto j = jsonser::json::parse(slurp(seg));
    std::ofstream(seg, std::ios::binary | std::ios::trunc) << j.dump(4);
    std::uint64_t i = 0;
    for (Rec& rec : r.stream<Rec>("recs")) CHECK(rec == make_rec(i++));
    CHECK_EQ(i, 50u);
    CHECK(r.read_at<Rec>("recs", 42) == make_rec(42));
}

TEST(incomplete_archive_has_no_manifest) {
    fs::path dir = temp_dir("incomplete");
    try {
        jsonser::segmented_writer w(dir);
        auto s = w.stream<Rec>("recs");
        s.push(make_rec(1));
        throw std::runtime_error("crash while writing");
    } catch (const std::runtime_error&) {
    }
    CHECK(!fs::exists(dir / "manifest.json"));
    CHECK_THROWS_WITH(jsonser::segmented_reader r(dir), "writer was not closed");
}

TEST(overwrite_replaces_old_archive) {
    fs::path dir = temp_dir("overwrite");
    write_archive(dir, 100);
    write_archive(dir, 10);
    jsonser::segmented_reader r(dir);
    CHECK_EQ(r.count("recs"), 10u);
    std::size_t files = 0;
    for (auto& f : fs::directory_iterator(dir)) files += f.is_regular_file();
    CHECK_EQ(files, 3u);  // manifest + header + one stream segment
}

TEST(writer_misuse) {
    fs::path dir = temp_dir("misuse");
    jsonser::segmented_writer w(dir);
    w.write("a", 1);
    CHECK_THROWS_WITH(w.write("a", 2), "duplicate entry name");
    {
        auto s = w.stream<int>("ints");
        CHECK_THROWS_WITH(w.close(), "still open");
        s.push(1);
    }  // closes the stream
    w.write_stream("more", std::vector<int>{1, 2, 3});
    w.close();
    jsonser::segmented_reader r(dir);
    CHECK_EQ(r.read<int>("a"), 1);
    CHECK_EQ(r.count("ints"), 1u);
    CHECK_EQ(r.read_at<int>("more", 2), 3);
}

TEST(empty_stream) {
    fs::path dir = temp_dir("empty");
    {
        jsonser::segmented_writer w(dir);
        w.stream<Rec>("none");
    }
    jsonser::segmented_reader r(dir);
    CHECK_EQ(r.count("none"), 0u);
    std::uint64_t n = 0;
    for (Rec& rec : r.stream<Rec>("none")) n += 1 + rec.id;
    CHECK_EQ(n, 0u);
}

TEST(unsafe_manifest_file_names_rejected) {
    fs::path dir = temp_dir("unsafe");
    fs::create_directories(dir);
    std::ofstream(dir / "manifest.json") << R"({"jsonser":{"format":1},"entries":[{"name":"x","kind":"object",
        "segments":[{"file":"../../secret.json"}]}]})";
    CHECK_THROWS_WITH(jsonser::segmented_reader r(dir), "unsafe file name");
}

struct Box {
    std::shared_ptr<int> a, b;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(a) & JSONSER_NVP(b);
    }
};

TEST(shared_pointers_are_per_record) {
    fs::path dir = temp_dir("shared");
    auto common = std::make_shared<int>(7);
    {
        jsonser::segmented_writer w(dir);
        auto s = w.stream<Box>("boxes");
        for (int i = 0; i < 3; ++i) s.push(Box{common, common});  // same pointer in every record
    }
    jsonser::segmented_reader r(dir);
    Box b2 = r.read_at<Box>("boxes", 2);  // each record stands alone: "$id" restarts per record
    CHECK(b2.a && b2.a == b2.b && *b2.a == 7);
    int n = 0;
    for (Box& b : r.stream<Box>("boxes")) n += (b.a == b.b && *b.a == 7);
    CHECK_EQ(n, 3);
}
