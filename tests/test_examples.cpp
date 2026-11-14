// Round-trips the classes the examples use (examples/model/*.hpp), so the examples stay correct.
#include <sstream>
#include <unordered_map>

#include <jsonser/jsonser.hpp>

#include "../examples/model/custom.hpp"
#include "../examples/model/player.hpp"
#include "../examples/model/scene.hpp"
#include "../examples/model/sensor.hpp"
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

TEST(player_with_owning_raw_pointers) {
    std::unordered_map<int, model::Player> players;
    model::Player p;
    p.name = "ann";
    p.role = model::Role::mage;
    p.inventory.add(new model::Item{"apple", 3});
    p.inventory.add(new model::Weapon{"sword", 12});
    p.stats = {{"str", 7}};
    players.emplace(0, std::move(p));

    std::string json = save_doc("players", players);
    CHECK(json.find("\"$type\":\"Weapon\"") != std::string::npos);
    CHECK(json.find("\"Player\":3") != std::string::npos);

    std::unordered_map<int, model::Player> back;
    load_doc(json, "players", back);
    load_doc(json, "players", back);  // loading again frees the old items first (no leak/double free)
    const auto& b = back.at(0);
    CHECK_EQ(b.name, "ann");
    CHECK(b.role == model::Role::mage);
    CHECK(!b.speed);
    CHECK_EQ(b.stats.at("str"), 7);
    CHECK(b.inventory.items().size() == 2);
    auto* w = dynamic_cast<const model::Weapon*>(b.inventory.items()[1]);
    CHECK(w && w->damage == 12 && w->qty == 1);
}

TEST(scene_shared_and_polymorphic) {
    model::Scene s;
    auto c = std::make_shared<model::Circle>();
    c->r = 2;
    s.background = std::make_unique<model::Rect>();
    s.shapes = {c};
    s.selected = c;
    model::Scene back;
    load_doc(save_doc("s", s), "s", back);
    CHECK(back.selected == back.shapes[0]);
    CHECK(dynamic_cast<model::Rect*>(back.background.get()) != nullptr);
    CHECK(back.shapes[0]->area() > 12.5);
}

TEST(custom_shapes) {
    thirdparty::Money m{-1205, "USD"};
    std::string json = save_doc("m", m);
    CHECK(json.find("\"amount\":\"-12.05\"") != std::string::npos);
    thirdparty::Money mb;
    load_doc(json, "m", mb);
    CHECK_EQ(mb.cents, -1205);
    CHECK_EQ(mb.currency, "USD");

    model::Settings st{"Low", model::Difficulty::hard, {1, 2.5, -3}};
    json = save_doc("st", st);
    CHECK(json.find("\"difficulty\":\"hard\"") != std::string::npos);
    CHECK(json.find("\"spawn\":[1,2.5,-3]") != std::string::npos);
    model::Settings sb;
    load_doc(json, "st", sb);
    CHECK(sb.difficulty() == model::Difficulty::hard);
    CHECK_EQ(sb.spawn().z, -3.0);
    CHECK_THROWS_WITH(load_doc(R"({"data":{"st":{"spawn":[1,2]}}})", "st", sb), "expected [x, y, z]");

    thirdparty::Timestamp t{2026, 11, 14}, tb;
    load_doc(save_doc("t", t), "t", tb);
    CHECK_EQ(tb.day, 14);
}

TEST(sample_v1_file_loads_with_v2_code) {
    model::Sample s;
    load_doc(R"({"data":{"s":{"t":1,"value":2.5,"sensor":"s1","t2":99}},"jsonser":{"format":1,"types":{"Sample":1}}})",
             "s", s);
    CHECK_EQ(s.t, 1u);
    CHECK_EQ(s.t2, 0u);  // a version-1 file: t2 is not read
}
