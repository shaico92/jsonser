#include <sstream>

#include <jsonser/jsonser.hpp>

#include "check.hpp"

template <class T>
static std::string save_doc(const char* name, const T& v) {
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os);
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
template <class T>
static T round_trip(const T& v) {
    T out{};
    load_doc(save_doc("v", v), "v", out);
    return out;
}

enum class Fruit { apple = 1, pear = 2 };

TEST(sequences) {
    std::vector<int> v{1, 2, 3};
    CHECK(round_trip(v) == v);
    std::vector<bool> vb{true, false, true};
    CHECK(round_trip(vb) == vb);
    std::deque<std::string> d{"a", "b"};
    CHECK(round_trip(d) == d);
    std::list<double> l{1.5, -2};
    CHECK(round_trip(l) == l);
    std::set<int> s{5, 1, 3};
    CHECK(round_trip(s) == s);
    std::unordered_set<std::string> us{"x", "y"};
    CHECK(round_trip(us) == us);
    std::multiset<int> ms{1, 1, 2};
    CHECK(round_trip(ms) == ms);
    std::array<int, 3> a{7, 8, 9};
    CHECK(round_trip(a) == a);
    std::vector<std::vector<int>> nested{{1}, {}, {2, 3}};
    CHECK(round_trip(nested) == nested);
    CHECK(jsonser::to_json(v) == "[1,2,3]");
}

struct Grid {
    int cells[2][3] = {};
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(cells);
    }
};
struct Img {
    std::vector<std::uint8_t> px;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::make_nvp("px", jsonser::as_base64(px));
    }
};

TEST(c_array_member) {
    Grid g{{{1, 2, 3}, {4, 5, 6}}};
    CHECK_EQ(jsonser::to_json(g), "{\"cells\":[[1,2,3],[4,5,6]]}");
    Grid g2;
    load_doc(save_doc("g", g), "g", g2);
    CHECK_EQ(g2.cells[1][2], 6);
}

TEST(fixed_size_mismatch_fails) {
    std::array<int, 3> a{};
    CHECK_THROWS_WITH(load_doc(R"({"data":{"a":[1,2]}})", "a", a), "expected 3 elements");
}

TEST(maps) {
    std::map<std::string, int> m{{"b", 2}, {"a", 1}};
    CHECK_EQ(jsonser::to_json(m), "{\"a\":1,\"b\":2}");
    CHECK(round_trip(m) == m);
    std::map<int, std::string> mi{{-3, "x"}, {10, "y"}};
    CHECK_EQ(jsonser::to_json(mi), "{\"-3\":\"x\",\"10\":\"y\"}");
    CHECK(round_trip(mi) == mi);
    std::unordered_map<Fruit, int> mf{{Fruit::pear, 4}};
    CHECK(round_trip(mf) == mf);
    std::map<double, int> md{{1.5, 1}};  // non-string-able key -> pairs
    CHECK_EQ(jsonser::to_json(md), "[[1.5,1]]");
    CHECK(round_trip(md) == md);
    std::multimap<std::string, int> mm{{"k", 1}, {"k", 2}};  // duplicates -> pairs
    CHECK(round_trip(mm) == mm);
    std::map<std::pair<int, int>, std::string> mp{{{1, 2}, "p"}};
    CHECK(round_trip(mp) == mp);
}

TEST(bad_map_key) {
    std::map<int, int> m;
    CHECK_THROWS_WITH(load_doc(R"({"data":{"m":{"x1":1}}})", "m", m), "not a valid integer");
}

TEST(pair_tuple_optional) {
    std::pair<int, std::string> p{1, "one"};
    CHECK(round_trip(p) == p);
    CHECK_EQ(jsonser::to_json(p), "[1,\"one\"]");
    std::tuple<int, double, std::string> t{1, 2.5, "z"};
    CHECK(round_trip(t) == t);
    std::optional<int> some = 5, none;
    CHECK(round_trip(some) == some);
    CHECK(!round_trip(none).has_value());
    CHECK_EQ(jsonser::to_json(none), "null");
}

struct Cat {
    std::string name;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(name);
    }
    bool operator==(const Cat& o) const { return name == o.name; }
};
JSONSER_CLASS_VERSION(Cat, 0)

TEST(variants) {
    using V = std::variant<std::monostate, int, std::string, Cat>;
    V a = 42, b = std::string("s"), c = Cat{"tom"}, d;
    CHECK(round_trip(a) == a);
    CHECK(round_trip(b) == b);
    CHECK(round_trip(c) == c);
    CHECK(round_trip(d) == d);
    CHECK_EQ(jsonser::to_json(c), "{\"i\":3,\"t\":\"Cat\",\"v\":{\"name\":\"tom\"}}");
    // The registered name wins over the index: reordered alternatives still load.
    std::variant<Cat, int> r;
    load_doc(R"({"data":{"v":{"i":3,"t":"Cat","v":{"name":"felix"}}}})", "v", r);
    CHECK(std::holds_alternative<Cat>(r) && std::get<Cat>(r).name == "felix");
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":{"i":9}}})", "v", a), "out of range");
}

TEST(base64) {
    std::vector<std::byte> bytes{std::byte{0}, std::byte{0xFF}, std::byte{0x10}, std::byte{'a'}};
    CHECK_EQ(jsonser::to_json(bytes), "\"AP8QYQ==\"");
    CHECK(round_trip(bytes) == bytes);
    for (std::size_t n = 0; n < 8; ++n) {  // every padding length
        std::string s(n, '\x7f');
        std::string enc = jsonser::base64_encode(s.data(), s.size());
        std::string dec;
        CHECK(jsonser::base64_decode(enc, dec));
        CHECK_EQ(dec, s);
    }
    Img img{{1, 2, 3}};
    CHECK_EQ(jsonser::to_json(img), "{\"px\":\"AQID\"}");
    Img back;
    load_doc(save_doc("i", img), "i", back);
    CHECK(back.px == img.px);
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":"@@@"}})", "v", bytes), "invalid base64");
}

// ---- pointers -----------------------------------------------------------------------------------
struct Node {
    int value = 0;
    std::shared_ptr<Node> next;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(value) & JSONSER_NVP(next);
    }
};

struct Animal {
    virtual ~Animal() = default;
    std::string name;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(name);
    }
    virtual std::string sound() const { return "..."; }
};
struct Dog : Animal {
    int bones = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Animal>(*this) & JSONSER_NVP(bones);
    }
    std::string sound() const override { return "woof"; }
};
struct Bird : Animal {
    bool flies = true;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Animal>(*this) & JSONSER_NVP(flies);
    }
    std::string sound() const override { return "tweet"; }
};
struct Unregistered : Animal {
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Animal>(*this);
    }
};
JSONSER_EXPORT(Dog, Animal)
JSONSER_EXPORT_NAMED(Bird, Animal, "bird")

TEST(unique_ptr_values) {
    std::unique_ptr<int> i = std::make_unique<int>(5), n;
    CHECK_EQ(jsonser::to_json(i), "5");
    CHECK_EQ(*round_trip(i), 5);
    CHECK(!round_trip(n));
    auto c = std::make_unique<Cat>(Cat{"kit"});
    CHECK_EQ(round_trip(c)->name, "kit");
}

TEST(shared_ptr_sharing_is_kept) {
    auto a = std::make_shared<Node>();
    a->value = 1;
    std::vector<std::shared_ptr<Node>> v{a, a, nullptr};
    std::string json = jsonser::to_json(v);
    CHECK_EQ(json, "[{\"$id\":1,\"value\":1,\"next\":null},{\"$ref\":1},null]");
    auto back = round_trip(v);
    CHECK(back.size() == 3 && back[0] && back[0] == back[1] && !back[2]);
    CHECK_EQ(back[0]->value, 1);
}

TEST(shared_ptr_cycle) {
    auto a = std::make_shared<Node>();
    auto b = std::make_shared<Node>();
    a->value = 1;
    b->value = 2;
    a->next = b;
    b->next = a;  // cycle
    auto back = round_trip(a);
    CHECK(back && back->next && back->next->next == back);
    CHECK_EQ(back->next->value, 2);
    a->next.reset();  // break the cycles so they free
    back->next->next.reset();
}

TEST(shared_ptr_of_non_object) {
    auto p = std::make_shared<std::vector<int>>(std::vector<int>{1, 2});
    std::vector<std::shared_ptr<std::vector<int>>> v{p, p};
    CHECK_EQ(jsonser::to_json(v), "[{\"$id\":1,\"$val\":[1,2]},{\"$ref\":1}]");
    auto back = round_trip(v);
    CHECK(back[0] == back[1] && (*back[0])[1] == 2);
}

TEST(polymorphic_pointers) {
    std::vector<std::shared_ptr<Animal>> zoo;
    auto d = std::make_shared<Dog>();
    d->name = "rex";
    d->bones = 3;
    auto b = std::make_shared<Bird>();
    b->name = "tweety";
    auto plain = std::make_shared<Animal>();
    plain->name = "blob";
    zoo = {d, b, plain, d};
    std::string json = jsonser::to_json(zoo);
    CHECK(json.find("{\"$id\":1,\"$type\":\"Dog\",\"name\":\"rex\",\"bones\":3}") != std::string::npos);
    CHECK(json.find("\"$type\":\"bird\"") != std::string::npos);
    auto back = round_trip(zoo);
    CHECK(back.size() == 4);
    CHECK_EQ(back[0]->sound(), "woof");
    CHECK_EQ(back[1]->sound(), "tweet");
    CHECK_EQ(back[2]->sound(), "...");
    CHECK(back[0] == back[3]);
    CHECK_EQ(static_cast<Dog&>(*back[0]).bones, 3);

    std::unique_ptr<Animal> u = std::make_unique<Bird>();
    CHECK_EQ(round_trip(u)->sound(), "tweet");

    std::shared_ptr<Animal> bad = std::make_shared<Unregistered>();
    CHECK_THROWS_WITH(save_doc("v", bad), "unregistered polymorphic type");
    std::shared_ptr<Animal> x;
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":{"$type":"Cow"}}})", "v", x), "unknown \"$type\" 'Cow'");
    CHECK_THROWS_WITH(load_doc(R"({"data":{"v":{"$ref":4}}})", "v", x), "points to no earlier");
}
