// jsonser - segmented archives for very large data.
//
//   save_dir/
//     manifest.json            <- written LAST; lists versions, entries and their segment files
//     00000_world.json         <- an "object" entry: one JSON value
//     00001_players.json       <- a "stream" entry: a JSON array, ONE RECORD PER LINE
//     00002_players.json          (rolls over to a new file every max_segment_bytes)
//
// Every segment file is complete, valid JSON. Readers in any language read manifest.json and
// open only the segments they need. See FORMAT.md.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core.hpp"
#include "iarchive.hpp"
#include "oarchive.hpp"
#include "writer.hpp"

namespace jsonser {

namespace fs = std::filesystem;

struct segment_info {
    std::string file;
    std::uint64_t bytes = 0;
    std::uint64_t count = 0;        ///< records in this segment (1 for an object entry)
    std::uint64_t first_index = 0;  ///< stream index of the first record in this segment
    std::uint32_t crc32 = 0;
};

struct entry_info {
    std::string name;
    std::string kind;  ///< "object" or "stream"
    std::string type;  ///< registered class name of the value / records ("" if unnamed)
    std::uint64_t count = 0;
    std::vector<segment_info> segments;
};

struct manifest {
    unsigned format = format_version;
    std::string created;
    version_map types;
    std::vector<entry_info> entries;

    const entry_info* find(std::string_view name) const {
        for (auto& e : entries)
            if (e.name == name) return &e;
        return nullptr;
    }
};

struct segment_options {
    std::uint64_t max_segment_bytes = 64ull << 20;  ///< a stream starts a new file after this many bytes
    int object_indent = 0;                          ///< pretty-print object entries (streams stay one-per-line)
};

struct reader_options {
    bool verify_crc = true;          ///< check CRC-32 of each segment read in full (object reads, stream())
    bool verify_on_read_at = false;  ///< read_at() also reads the rest of the segment to check its CRC
};

namespace detail {

inline std::string utc_now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

inline std::string file_safe(std::string_view name) {
    std::string s;
    for (char c : name.substr(0, 40))
        s += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? c : '_';
    return s;
}

/// A manifest may come from anywhere: only plain file names inside the archive folder are allowed.
inline bool plain_file_name(const std::string& f) {
    return !f.empty() && f.find_first_of("/\\:") == std::string::npos && f != "." && f != "..";
}

template <class T>
std::string type_name_of() {
    if constexpr (has_class_name<T>())
        return class_traits<T>::name;
    else
        return {};
}

inline void write_manifest(const fs::path& file, const manifest& m) {
    std::ofstream os(file, std::ios::binary | std::ios::trunc);
    if (!os) throw archive_error("jsonser: cannot create " + file.string());
    writer w(os, 2);
    w.begin_object();
    w.key("jsonser");
    w.begin_object();
    w.key("format");
    w.value_uint(m.format);
    w.key("layout");
    w.value_string("segmented");
    w.end_object();
    w.key("created");
    w.value_string(m.created);
    w.key("types");
    w.begin_object();
    for (auto& [n, v] : m.types) {
        w.key(n);
        w.value_uint(v);
    }
    w.end_object();
    w.key("entries");
    w.begin_array();
    for (auto& e : m.entries) {
        w.begin_object();
        w.key("name"); w.value_string(e.name);
        w.key("kind"); w.value_string(e.kind);
        if (!e.type.empty()) { w.key("type"); w.value_string(e.type); }
        w.key("count"); w.value_uint(e.count);
        w.key("segments");
        w.begin_array();
        for (auto& s : e.segments) {
            w.begin_object();
            w.key("file"); w.value_string(s.file);
            w.key("bytes"); w.value_uint(s.bytes);
            w.key("count"); w.value_uint(s.count);
            w.key("first_index"); w.value_uint(s.first_index);
            w.key("crc32"); w.value_uint(s.crc32);
            w.end_object();
        }
        w.end_array();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    w.raw("\n");
    w.flush();
    os.close();
    if (!os) throw archive_error("jsonser: failed writing " + file.string());
}

inline manifest read_manifest(const fs::path& file) {
    std::ifstream is(file, std::ios::binary);
    if (!is)
        throw archive_error("jsonser: no " + file.string() +
                            " - not a segmented archive, or the writer was not closed (incomplete)");
    json j;
    try {
        j = json::parse(is);
    } catch (const json::exception& e) {
        throw archive_error("jsonser: invalid manifest " + file.string() + ": " + e.what());
    }
    manifest m;
    try {
        m.format = j.at("jsonser").at("format").get<unsigned>();
        if (m.format > format_version)
            throw archive_error("jsonser: archive format " + std::to_string(m.format) + " is newer than this library");
        m.created = j.value("created", "");
        if (j.contains("types")) m.types = read_types(j["types"]);
        for (auto& je : j.at("entries")) {
            entry_info e;
            e.name = je.at("name").get<std::string>();
            e.kind = je.at("kind").get<std::string>();
            e.type = je.value("type", "");
            e.count = je.value("count", std::uint64_t{0});
            for (auto& js : je.at("segments")) {
                segment_info s;
                s.file = js.at("file").get<std::string>();
                if (!plain_file_name(s.file)) throw archive_error("jsonser: manifest has an unsafe file name: " + s.file);
                s.bytes = js.value("bytes", std::uint64_t{0});
                s.count = js.value("count", std::uint64_t{0});
                s.first_index = js.value("first_index", std::uint64_t{0});
                s.crc32 = js.value("crc32", std::uint32_t{0});
                e.segments.push_back(std::move(s));
            }
            m.entries.push_back(std::move(e));
        }
    } catch (const json::exception& e) {
        throw archive_error(std::string("jsonser: malformed manifest: ") + e.what());
    }
    return m;
}

}  // namespace detail

class segmented_writer;

/// Appends records of type T to a stream entry. Get one from segmented_writer::stream<T>(name).
template <class T>
class stream_writer {
public:
    stream_writer(stream_writer&& o) noexcept
        : parent_(std::exchange(o.parent_, nullptr)),
          entry_(std::move(o.entry_)),
          file_(std::move(o.file_)),
          w_(std::move(o.w_)),
          ar_(std::move(o.ar_)),
          seg_(std::move(o.seg_)),
          uncaught_(o.uncaught_) {}
    stream_writer(const stream_writer&) = delete;
    stream_writer& operator=(const stream_writer&) = delete;
    stream_writer& operator=(stream_writer&&) = delete;

    ~stream_writer() {
        if (parent_ && std::uncaught_exceptions() == uncaught_) {
            try {
                close();
            } catch (...) {
            }
        }
    }

    void push(const T& value);
    void close();
    std::uint64_t count() const { return entry_.count; }

private:
    friend class segmented_writer;
    stream_writer(segmented_writer& parent, std::string name);
    void open_segment();
    void close_segment();

    segmented_writer* parent_;
    entry_info entry_;
    std::unique_ptr<std::ofstream> file_;
    std::unique_ptr<writer> w_;
    std::unique_ptr<json_oarchive> ar_;
    segment_info seg_;
    int uncaught_ = std::uncaught_exceptions();
};

class segmented_writer {
public:
    explicit segmented_writer(const fs::path& dir, segment_options opt = {})
        : dir_(dir), opt_(opt), uncaught_(std::uncaught_exceptions()) {
        fs::create_directories(dir_);
        // Overwriting an old archive: remove the old manifest first (so a crash never leaves a
        // manifest pointing at a mix of files), then the segment files it listed.
        fs::path old = dir_ / "manifest.json";
        if (fs::exists(old)) {
            manifest m;
            try {
                m = detail::read_manifest(old);
            } catch (...) {
            }
            fs::remove(old);
            for (auto& e : m.entries)
                for (auto& s : e.segments) {
                    std::error_code ec;
                    fs::remove(dir_ / s.file, ec);
                }
        }
    }
    segmented_writer(const segmented_writer&) = delete;
    segmented_writer& operator=(const segmented_writer&) = delete;

    /// Closes (writes manifest.json) unless an exception is unwinding - then the archive stays
    /// visibly incomplete (no manifest) instead of looking valid.
    ~segmented_writer() {
        if (!closed_ && std::uncaught_exceptions() == uncaught_) {
            try {
                close();
            } catch (...) {
            }
        }
    }

    /// Writes one value as its own segment file ("object" entry).
    template <class T>
    void write(const std::string& name, const T& value) {
        claim_name(name);
        entry_info e;
        e.name = name;
        e.kind = "object";
        e.type = detail::type_name_of<T>();
        segment_info s;
        s.file = next_file(name);
        {
            std::ofstream os(dir_ / s.file, std::ios::binary | std::ios::trunc);
            if (!os) throw archive_error("jsonser: cannot create " + (dir_ / s.file).string());
            writer w(os, opt_.object_indent);
            json_oarchive ar(w, types_);
            ar.save_value(value);
            w.raw("\n");
            s.bytes = w.bytes();
            s.crc32 = w.crc();
            os.close();
            if (!os) throw archive_error("jsonser: failed writing " + (dir_ / s.file).string());
        }
        s.count = 1;
        e.count = 1;
        e.segments.push_back(std::move(s));
        entries_.push_back(std::move(e));
    }

    /// Opens a stream entry: push() records one at a time; files roll over by size.
    template <class T>
    stream_writer<T> stream(const std::string& name) {
        claim_name(name);
        ++open_streams_;
        return stream_writer<T>(*this, name);
    }

    /// Convenience: writes a whole range as a stream entry.
    template <class Range>
    void write_stream(const std::string& name, const Range& r) {
        using T = remove_cvref_t<decltype(*std::begin(r))>;
        auto s = stream<T>(name);
        for (const auto& v : r) s.push(v);
        s.close();
    }

    /// Writes manifest.json (to a temp file, then renamed). Only now is the archive complete.
    void close() {
        if (closed_) return;
        if (open_streams_ > 0) throw archive_error("jsonser: segmented_writer::close() while a stream is still open");
        manifest m;
        m.created = detail::utc_now_iso();
        for (auto& [n, v] : types_.to_map()) m.types.emplace(n, v);
        m.entries = entries_;
        fs::path tmp = dir_ / "manifest.json.tmp";
        detail::write_manifest(tmp, m);
        fs::rename(tmp, dir_ / "manifest.json");
        closed_ = true;
    }

    const fs::path& directory() const { return dir_; }

private:
    template <class T>
    friend class stream_writer;

    void claim_name(const std::string& name) {
        if (closed_) throw archive_error("jsonser: segmented_writer is closed");
        if (!names_.insert(name).second) throw archive_error("jsonser: duplicate entry name '" + name + "'");
    }
    std::string next_file(const std::string& name) {
        char num[16];
        std::snprintf(num, sizeof num, "%05u", file_counter_++);
        return std::string(num) + "_" + detail::file_safe(name) + ".json";
    }
    void finish_stream(entry_info e) {
        --open_streams_;
        entries_.push_back(std::move(e));
    }

    fs::path dir_;
    segment_options opt_;
    type_table types_;
    std::vector<entry_info> entries_;
    std::set<std::string> names_;
    unsigned file_counter_ = 0;
    int open_streams_ = 0;
    bool closed_ = false;
    int uncaught_;
};

template <class T>
stream_writer<T>::stream_writer(segmented_writer& parent, std::string name) : parent_(&parent) {
    entry_.name = std::move(name);
    entry_.kind = "stream";
    entry_.type = detail::type_name_of<T>();
}

template <class T>
void stream_writer<T>::push(const T& value) {
    if (!parent_) throw archive_error("jsonser: push() on a closed stream");
    if (!file_) open_segment();
    if (seg_.count > 0) w_->raw(",\n");
    w_->reset_separator();
    ar_->reset_tracking();  // shared_ptr sharing is per record: every record reads on its own
    ar_->save_value(value);
    ++seg_.count;
    ++entry_.count;
    if (w_->bytes() >= parent_->opt_.max_segment_bytes) close_segment();
}

template <class T>
void stream_writer<T>::close() {
    if (!parent_) return;
    if (file_) close_segment();
    auto* p = std::exchange(parent_, nullptr);
    p->finish_stream(std::move(entry_));
}

template <class T>
void stream_writer<T>::open_segment() {
    seg_ = segment_info{};
    seg_.file = parent_->next_file(entry_.name);
    seg_.first_index = entry_.count;
    file_ = std::make_unique<std::ofstream>(parent_->dir_ / seg_.file, std::ios::binary | std::ios::trunc);
    if (!*file_) throw archive_error("jsonser: cannot create " + (parent_->dir_ / seg_.file).string());
    w_ = std::make_unique<writer>(*file_, 0);
    ar_ = std::make_unique<json_oarchive>(*w_, parent_->types_);
    w_->raw("[\n");
}

template <class T>
void stream_writer<T>::close_segment() {
    w_->raw(seg_.count > 0 ? "\n]\n" : "]\n");
    seg_.bytes = w_->bytes();
    seg_.crc32 = w_->crc();
    ar_.reset();
    w_.reset();
    file_->close();
    bool ok = !file_->fail();
    file_.reset();
    if (!ok) throw archive_error("jsonser: failed writing " + seg_.file);
    entry_.segments.push_back(std::move(seg_));
    seg_ = segment_info{};
}

// =============================================================================================
// Reading
// =============================================================================================
namespace detail {

/// Reads one stream segment record by record (one JSON line each), checking CRC and count.
/// Falls back to parsing the whole file if another tool re-formatted it.
class segment_cursor {
public:
    segment_cursor(const fs::path& file, const segment_info& info, bool verify)
        : file_(file), info_(info), verify_(verify), in_(file, std::ios::binary) {
        if (!in_) throw archive_error("jsonser: missing segment file " + file.string());
        if (!read_line()) fail("empty file");
        // Our one-record-per-line layout is trusted only if the file is byte-for-byte the size
        // the writer recorded; anything re-formatted by another tool is parsed as a whole.
        std::error_code ec;
        const bool same_size = fs::file_size(file, ec) == info.bytes && !ec;
        if (line_ != "[" || !same_size) {
            std::string rest = line_ + (in_.eof() ? "" : "\n");
            rest.append(std::istreambuf_iterator<char>(in_), std::istreambuf_iterator<char>());
            if (verify_) crc_ = crc32{}, crc_.update(rest.data(), rest.size());
            try {
                dom_ = json::parse(rest);
            } catch (const json::exception& e) {
                fail(std::string("invalid JSON: ") + e.what());
            }
            if (!dom_.is_array()) fail("expected a JSON array");
            dom_mode_ = true;
        }
    }

    /// Parses the next record into `out`. Returns false (after checking count + CRC) at the end.
    bool next(json& out) {
        if (dom_mode_) {
            if (dom_idx_ >= dom_.size()) return end();
            out = std::move(dom_[dom_idx_++]);
            ++seen_;
            return true;
        }
        if (!read_line()) fail("truncated (no closing ']')");
        if (line_ == "]") return end();
        std::size_t n = line_.size();
        if (n && line_[n - 1] == ',') --n;
        try {
            out = json::parse(line_.data(), line_.data() + n);
        } catch (const json::exception& e) {
            fail("record " + std::to_string(info_.first_index + seen_) + ": invalid JSON: " + e.what());
        }
        ++seen_;
        return true;
    }

    /// Skips `k` records without parsing them.
    void skip(std::uint64_t k) {
        if (dom_mode_) {
            dom_idx_ += static_cast<std::size_t>(k);
            seen_ += k;
            return;
        }
        for (std::uint64_t i = 0; i < k; ++i) {
            if (verify_) {
                if (!read_line() || line_ == "]") fail("fewer records than the manifest says");
            } else {
                in_.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (!in_) fail("fewer records than the manifest says");
            }
            ++seen_;
        }
    }

    /// Reads to the end of the file to finish the CRC check (used by read_at with verification).
    void drain_and_verify() {
        json tmp;
        while (next(tmp)) {
        }
    }

private:
    bool read_line() {
        if (!std::getline(in_, line_)) return false;
        if (verify_) {
            crc_.update(line_.data(), line_.size());
            if (!in_.eof()) crc_.update("\n", 1);
        }
        return true;
    }
    bool end() {
        if (verify_) {
            if (!dom_mode_) {  // trailing bytes after ']' are part of the file too
                std::string rest((std::istreambuf_iterator<char>(in_)), std::istreambuf_iterator<char>());
                crc_.update(rest.data(), rest.size());
            }
            if (crc_.value() != info_.crc32)
                fail("CRC-32 mismatch (file is corrupted or was modified after writing)");
        }
        if (seen_ != info_.count)
            fail("has " + std::to_string(seen_) + " records, manifest says " + std::to_string(info_.count));
        return false;
    }
    [[noreturn]] void fail(const std::string& msg) const {
        throw archive_error("jsonser: segment " + file_.filename().string() + ": " + msg);
    }

    fs::path file_;
    segment_info info_;
    bool verify_;
    std::ifstream in_;
    std::string line_;
    crc32 crc_;
    json dom_;
    std::size_t dom_idx_ = 0;
    bool dom_mode_ = false;
    std::uint64_t seen_ = 0;
};

}  // namespace detail

class segmented_reader;

/// Input range over a stream entry: loads one segment at a time and one record at a time.
template <class T>
class stream_range {
public:
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        iterator() = default;
        explicit iterator(stream_range* r) : r_(r) {}
        T& operator*() const { return r_->value_; }
        T* operator->() const { return &r_->value_; }
        iterator& operator++() {
            if (!r_->advance()) r_ = nullptr;
            return *this;
        }
        bool operator==(const iterator& o) const { return r_ == o.r_; }
        bool operator!=(const iterator& o) const { return r_ != o.r_; }

    private:
        stream_range* r_ = nullptr;
    };

    iterator begin() {
        if (started_) throw archive_error("jsonser: a stream range can be iterated only once");
        started_ = true;
        return advance() ? iterator(this) : iterator();
    }
    iterator end() { return iterator(); }

    /// Index (in the whole stream) of the record currently loaded.
    std::uint64_t index() const { return index_ - 1; }

private:
    friend class segmented_reader;
    stream_range(fs::path dir, const entry_info& e, const version_map& types, bool verify)
        : dir_(std::move(dir)), entry_(&e), types_(&types), verify_(verify) {}

    bool advance() {
        for (;;) {
            if (!cursor_) {
                if (seg_ >= entry_->segments.size()) return false;
                cursor_ = std::make_unique<detail::segment_cursor>(dir_ / entry_->segments[seg_].file,
                                                                   entry_->segments[seg_], verify_);
            }
            if (cursor_->next(node_)) break;
            cursor_.reset();
            ++seg_;
        }
        std::string path = entry_->name + "[" + std::to_string(index_) + "]";
        if (!ar_)
            ar_ = std::make_unique<json_iarchive>(node_, *types_, std::move(path));
        else
            ar_->reset_root(node_, std::move(path));
        value_ = T{};  // fields missing from this record must not keep the previous record's values
        ar_->load_value(value_);
        ++index_;
        return true;
    }

    fs::path dir_;
    const entry_info* entry_;
    const version_map* types_;
    bool verify_;
    bool started_ = false;
    std::size_t seg_ = 0;
    std::uint64_t index_ = 0;
    std::unique_ptr<detail::segment_cursor> cursor_;
    json node_;
    std::unique_ptr<json_iarchive> ar_;
    T value_{};
};

class segmented_reader {
public:
    explicit segmented_reader(const fs::path& dir, reader_options opt = {})
        : dir_(dir), opt_(opt), m_(detail::read_manifest(dir / "manifest.json")) {}

    const manifest& info() const { return m_; }
    bool contains(std::string_view name) const { return m_.find(name) != nullptr; }
    std::uint64_t count(std::string_view name) const { return entry(name, nullptr).count; }

    /// Reads an "object" entry.
    template <class T>
    void read(std::string_view name, T& out) const {
        const entry_info& e = entry(name, "object");
        const segment_info& s = e.segments.at(0);
        fs::path file = dir_ / s.file;
        std::ifstream is(file, std::ios::binary);
        if (!is) throw archive_error("jsonser: missing segment file " + file.string());
        std::string text((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        if (opt_.verify_crc) {
            crc32 c;
            c.update(text.data(), text.size());
            if (c.value() != s.crc32)
                throw archive_error("jsonser: segment " + s.file +
                                    ": CRC-32 mismatch (file is corrupted or was modified after writing)");
        }
        json root;
        try {
            root = json::parse(text);
        } catch (const json::exception& ex) {
            throw archive_error("jsonser: segment " + s.file + ": invalid JSON: " + ex.what());
        }
        json_iarchive ar(root, m_.types, std::string(name));
        ar.load_value(out);
    }
    template <class T>
    T read(std::string_view name) const {
        T t{};
        read(name, t);
        return t;
    }

    /// Iterates a "stream" entry: `for (Player& p : reader.stream<Player>("players"))`.
    template <class T>
    stream_range<T> stream(std::string_view name) const {
        return stream_range<T>(dir_, entry(name, "stream"), m_.types, opt_.verify_crc);
    }

    template <class T, class F>
    void for_each(std::string_view name, F&& f) const {
        for (T& v : stream<T>(name)) f(v);
    }

    /// Random access into a stream: finds the segment from the manifest, skips lines, parses one.
    template <class T>
    T read_at(std::string_view name, std::uint64_t index) const {
        T t{};
        read_at(name, index, t);
        return t;
    }
    template <class T>
    void read_at(std::string_view name, std::uint64_t index, T& out) const {
        const entry_info& e = entry(name, "stream");
        if (index >= e.count)
            throw archive_error("jsonser: " + e.name + "[" + std::to_string(index) + "] out of range (count " +
                                std::to_string(e.count) + ")");
        auto it = std::upper_bound(e.segments.begin(), e.segments.end(), index,
                                   [](std::uint64_t i, const segment_info& s) { return i < s.first_index; });
        const segment_info& s = *std::prev(it);
        detail::segment_cursor cur(dir_ / s.file, s, opt_.verify_on_read_at);
        cur.skip(index - s.first_index);
        json node;
        if (!cur.next(node)) throw archive_error("jsonser: " + e.name + "[" + std::to_string(index) + "] not found");
        if (opt_.verify_on_read_at) cur.drain_and_verify();
        json_iarchive ar(node, m_.types, e.name + "[" + std::to_string(index) + "]");
        ar.load_value(out);
    }

private:
    const entry_info& entry(std::string_view name, const char* kind) const {
        const entry_info* e = m_.find(name);
        if (!e) throw archive_error("jsonser: archive has no entry '" + std::string(name) + "'");
        if (kind && e->kind != kind)
            throw archive_error("jsonser: entry '" + std::string(name) + "' is a " + e->kind + ", not a " + kind);
        return *e;
    }

    fs::path dir_;
    reader_options opt_;
    manifest m_;
};

}  // namespace jsonser
