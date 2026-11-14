#include <sstream>

#include <jsonser/jsonser.hpp>

#include "check.hpp"

// ---- model types ------------------------------------------------------------------------------
struct Point {
    int x = 0, y = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(x) & JSONSER_NVP(y);
    }
    bool operator==(const Point& o) const { return x == o.x && y == o.y; }
};

class Secret {  // private serialize via friend access
public:
    Secret() = default;
    explicit Secret(std::string s) : code_(std::move(s)) {}
    const std::string& code() const { return code_; }

private:
    friend class jsonser::access;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::make_nvp("code", code_);
    }
    std::string code_;
};

namespace geo {
struct Color {  // non-intrusive free serialize, found by ADL
    unsigned char r = 0, g = 0, b = 0;
};
template <class Ar>
void serialize(Ar& ar, Color& c, unsigned) {
    ar & jsonser::make_nvp("r", c.r) & jsonser::make_nvp("g", c.g) & jsonser::make_nvp("b", c.b);
}

struct Temperature {  // free split save/load: stored in Fahrenheit, used in Celsius
    double celsius = 0;
};
template <class Ar>
void save(Ar& ar, const Temperature& t, unsigned) {
    double f = t.celsius * 9 / 5 + 32;
    ar & jsonser::make_nvp("fahrenheit", f);
}
template <class Ar>
void load(Ar& ar, Temperature& t, unsigned) {
    double f = 0;
    ar & jsonser::make_nvp("fahrenheit", f);
    t.celsius = (f - 32) * 5 / 9;
}
JSONSER_SPLIT_FREE(Temperature)
}  // namespace geo

enum class Mode : std::uint8_t { off = 0, on = 1, turbo = 7 };

struct Settings {  // member split save/load
    Mode mode = Mode::off;
    template <class Ar>
    void save(Ar& ar, unsigned) const {
        std::string s = mode == Mode::on ? "on" : mode == Mode::turbo ? "turbo" : "off";
        ar & jsonser::make_nvp("mode", s);
    }
    template <class Ar>
    void load(Ar& ar, unsigned) {
        std::string s;
        ar & jsonser::make_nvp("mode", s);
        mode = s == "on" ? Mode::on : s == "turbo" ? Mode::turbo : Mode::off;
    }
    JSONSER_SPLIT_MEMBER()
};

struct Shape {
    std::string label;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(label);
    }
};
struct Circle : Shape {
    double radius = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Shape>(*this) & JSONSER_NVP(radius);
    }
};
JSONSER_CLASS_VERSION(Shape, 3)

// Versioning: the same JSON name "Account" with two layouts (old app / new app).
struct AccountV1 {
    std::string user;
    int balance = 0;  // whole dollars
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(user) & JSONSER_NVP(balance);
    }
};
JSONSER_CLASS_NAME_VERSION(AccountV1, "Account", 1)

struct Account {
    std::string user;
    std::int64_t cents = 0;   // v2: balance in cents, field renamed
    std::string currency = "USD";  // v2: new field
    unsigned loaded_version = 99;
    template <class Ar>
    void serialize(Ar& ar, unsigned version) {
        loaded_version = version;
        ar & JSONSER_NVP(user);
        if (version < 2) {
            int balance = 0;
            ar & JSONSER_NVP(balance);
            if constexpr (Ar::is_loading::value) cents = std::int64_t(balance) * 100;
        } else {
            ar & JSONSER_NVP(cents) & JSONSER_NVP(currency);
        }
    }
};
JSONSER_CLASS_NAME_VERSION(Account, "Account", 2)

struct Vec2 {
    float x = 0, y = 0;
};
// Custom JSON shape: [x, y] instead of {"x":..,"y":..}
template <>
struct jsonser::serializer<Vec2> {
    template <class Ar>
    static void save(Ar& ar, const Vec2& v) {
        ar.out().begin_array();
        ar.save_value(v.x);
        ar.save_value(v.y);
        ar.out().end_array();
    }
    template <class Ar>
    static void load(Ar& ar, Vec2& v) {
        const auto& n = ar.node();
        if (!n.is_array() || n.size() != 2) ar.fail("expected [x, y]");
        ar.load_element(n[0], v.x, 0);
        ar.load_element(n[1], v.y, 1);
    }
};

// ---- helpers ------------------------------------------------------------------------------------
template <class T>
static std::string save_doc(const char* name, const T& v, int indent = 0) {
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os, {indent});
        oa << jsonser::make_nvp(name, v);
    }
    return os.str();
}
template <class T>
static void load_doc(const std::string& text, const char* name, T& v) {
    std::istringstream is(text);
    jsonser::json_iarchive ia(is);
    ia >> jsonser::make_nvp(name, v);
}

// ---- tests ----------------------------------------------------------------------------------------
TEST(document_layout) {
    Point p{3, -4};
    CHECK_EQ(save_doc("p", p), "{\"data\":{\"p\":{\"x\":3,\"y\":-4}},\"jsonser\":{\"format\":1,\"types\":{}}}\n");
}

TEST(round_trip_primitives) {
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os);
        bool b = true;
        char c = 'A';
        std::int8_t i8 = -5;
        std::uint64_t big = 18446744073709551615ull;
        float f = 1.25f;
        double d = -2.5e-10;
        std::string s = "hi";
        Mode m = Mode::turbo;
        oa << JSONSER_NVP(b) << JSONSER_NVP(c) << JSONSER_NVP(i8) << JSONSER_NVP(big) << JSONSER_NVP(f)
           << JSONSER_NVP(d) << JSONSER_NVP(s) << JSONSER_NVP(m);
    }
    std::istringstream is(os.str());
    jsonser::json_iarchive ia(is);
    bool b = false;
    char c = 0;
    std::int8_t i8 = 0;
    std::uint64_t big = 0;
    float f = 0;
    double d = 0;
    std::string s;
    Mode m = Mode::off;
    ia >> JSONSER_NVP(b) >> JSONSER_NVP(c) >> JSONSER_NVP(i8) >> JSONSER_NVP(big) >> JSONSER_NVP(f) >> JSONSER_NVP(d) >>
        JSONSER_NVP(s) >> JSONSER_NVP(m);
    CHECK(b);
    CHECK_EQ(c, 'A');
    CHECK_EQ(i8, -5);
    CHECK_EQ(big, 18446744073709551615ull);
    CHECK_EQ(f, 1.25f);
    CHECK_EQ(d, -2.5e-10);
    CHECK_EQ(s, "hi");
    CHECK(m == Mode::turbo);
}

TEST(private_free_and_split) {
    Secret sec("xyz");
    geo::Color col{1, 2, 255};
    geo::Temperature t{100};
    Settings st{Mode::on};
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os);
        oa << JSONSER_NVP(sec) << JSONSER_NVP(col) << JSONSER_NVP(t) << JSONSER_NVP(st);
    }
    CHECK(os.str().find("\"fahrenheit\":212") != std::string::npos);
    CHECK(os.str().find("\"mode\":\"on\"") != std::string::npos);
    std::istringstream is(os.str());
    jsonser::json_iarchive ia(is);
    Secret sec2;
    geo::Color col2;
    geo::Temperature t2;
    Settings st2;
    ia >> jsonser::make_nvp("sec", sec2) >> jsonser::make_nvp("col", col2) >> jsonser::make_nvp("t", t2) >>
        jsonser::make_nvp("st", st2);
    CHECK_EQ(sec2.code(), "xyz");
    CHECK(col2.r == 1 && col2.g == 2 && col2.b == 255);
    CHECK(std::abs(t2.celsius - 100) < 1e-9);
    CHECK(st2.mode == Mode::on);
}

TEST(base_object_is_flat_and_versioned) {
    Circle c;
    c.label = "wheel";
    c.radius = 0.5;
    std::string s = save_doc("c", c);
    CHECK(s.find("{\"c\":{\"label\":\"wheel\",\"radius\":0.5}}") != std::string::npos);
    CHECK(s.find("\"Shape\":3") != std::string::npos);
    Circle c2;
    load_doc(s, "c", c2);
    CHECK_EQ(c2.label, "wheel");
    CHECK_EQ(c2.radius, 0.5);
}

TEST(versioning_old_file_new_code) {
    AccountV1 old{"dana", 12};
    std::string s = save_doc("acct", old);
    CHECK(s.find("\"Account\":1") != std::string::npos);
    Account now;
    load_doc(s, "acct", now);
    CHECK_EQ(now.loaded_version, 1u);
    CHECK_EQ(now.user, "dana");
    CHECK_EQ(now.cents, 1200);
    CHECK_EQ(now.currency, "USD");  // new field kept its default
}

TEST(versioning_new_file_old_code) {
    Account now;
    now.user = "eli";
    now.cents = 550;
    now.currency = "EUR";
    std::string s = save_doc("acct", now);
    CHECK(s.find("\"Account\":2") != std::string::npos);
    AccountV1 old;
    old.balance = -1;
    load_doc(s, "acct", old);  // unknown fields ignored, missing "balance" keeps its value
    CHECK_EQ(old.user, "eli");
    CHECK_EQ(old.balance, -1);
}

TEST(missing_and_extra_fields) {
    Point p{7, 8};
    load_doc(R"({"data":{"p":{"y":2,"z":99,"extra":{"deep":[1,2]}}}})", "p", p);
    CHECK_EQ(p.x, 7);  // missing -> unchanged
    CHECK_EQ(p.y, 2);
}

TEST(errors_carry_a_path) {
    Point p;
    CHECK_THROWS_WITH(load_doc(R"({"data":{"p":{"x":"seven"}}})", "p", p), "data.p.x: expected an integer");
    CHECK_THROWS_WITH(load_doc(R"({"data":{"q":{}}})", "p", p), "no top-level entry 'p'");
    CHECK_THROWS_WITH(load_doc(R"({"data":{"p":[1]}})", "p", p), "data.p: expected an object");
    CHECK_THROWS_WITH(load_doc("{\"data\":", "p", p), "invalid JSON");
    CHECK_THROWS_WITH(load_doc(R"({"nope":1})", "p", p), "not a jsonser document");
    std::int8_t small = 0;
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":300}})", "v", small), "out of range");
    unsigned u = 0;
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":-1}})", "v", u), "negative integer");
    CHECK_THROWS_WITH(load_doc(R"({"data":{},"jsonser":{"format":99}})", "v", u), "newer");
}

TEST(integers_written_as_floats_by_other_apps) {
    int v = 0;
    load_doc(R"({"data":{"v":42.0}})", "v", v);
    CHECK_EQ(v, 42);
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":42.5}})", "v", v), "expected an integer");
}

TEST(custom_serializer_shape) {
    Vec2 v{1.5f, -2};
    std::string s = save_doc("v", v);
    CHECK(s.find("\"v\":[1.5,-2]") != std::string::npos);
    Vec2 v2;
    load_doc(s, "v", v2);
    CHECK(v2.x == 1.5f && v2.y == -2);
}

TEST(pretty_document_parses_back) {
    Circle c;
    c.label = "p";
    c.radius = 2;
    std::string s = save_doc("c", c, 2);
    CHECK(s.find("\n  \"data\": {") != std::string::npos);
    Circle c2;
    load_doc(s, "c", c2);
    CHECK_EQ(c2.radius, 2);
}

TEST(nan_round_trip) {
    double d = std::numeric_limits<double>::quiet_NaN();
    std::string s = save_doc("d", d);
    double back = 0;
    load_doc(s, "d", back);
    CHECK(std::isnan(back));
}

TEST(to_json_fragment) {
    CHECK_EQ(jsonser::to_json(Point{1, 2}), "{\"x\":1,\"y\":2}");
}

// ---- loading speed-ups: read the stream into a buffer, move strings out of owned DOMs --------
TEST(load_from_middle_of_stream) {
    // The archive reads from the stream's current position, not from the start.
    std::stringstream ss;
    ss << "IGNORED" << save_doc("p", Point{3, 4});
    ss.ignore(7);
    jsonser::json_iarchive ia(ss);
    Point p;
    ia >> jsonser::make_nvp("p", p);
    CHECK(p == (Point{3, 4}));
}

TEST(load_from_unseekable_stream) {
    struct no_seek : std::stringbuf {  // like a pipe: seeking fails
        using std::stringbuf::stringbuf;
        pos_type seekoff(off_type, std::ios::seekdir, std::ios::openmode) override { return pos_type(-1); }
        pos_type seekpos(pos_type, std::ios::openmode) override { return pos_type(-1); }
    };
    std::string big(200000, 'x');  // bigger than one 64 KB read chunk
    no_seek buf(save_doc("s", big));
    std::istream is(&buf);
    jsonser::json_iarchive ia(is);
    std::string back;
    ia >> jsonser::make_nvp("s", back);
    CHECK(back == big);
}

struct ReadsTwice {  // a load() that reads the same field twice
    std::string a, b;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::make_nvp("name", a);
        if constexpr (Ar::is_loading::value) ar & jsonser::make_nvp("name", b);
    }
};

TEST(string_read_twice_from_stream_is_a_clear_error) {
    // A stream archive moves each string out of its JSON, so a second read fails loudly
    // instead of quietly giving "".
    ReadsTwice r;
    r.a = "Ka";
    std::string s = save_doc("r", r);
    ReadsTwice back;
    CHECK_THROWS_WITH(load_doc(s, "r", back), "data.r.name: expected a string, got a string that was already loaded");
}

TEST(caller_dom_is_not_modified) {
    // An archive over a DOM you pass in copies strings, so the DOM stays intact.
    ReadsTwice r;
    r.a = "Ka";
    const jsonser::json doc = jsonser::json::parse(save_doc("r", r));
    jsonser::json_iarchive ia(doc);
    ReadsTwice back;
    ia >> jsonser::make_nvp("r", back);
    CHECK_EQ(back.a, "Ka");
    CHECK_EQ(back.b, "Ka");
    CHECK_EQ(doc["data"]["r"]["name"].get<std::string>(), "Ka");
}

// Boost spellings of the direction flags all compile.
static_assert(jsonser::json_iarchive::is_loading::value && !jsonser::json_iarchive::is_saving::value);
static_assert(jsonser::json_oarchive::is_saving::value && !jsonser::json_oarchive::is_loading::value);
static_assert(jsonser::json_iarchive::is_loading() && jsonser::json_oarchive::is_saving{});
