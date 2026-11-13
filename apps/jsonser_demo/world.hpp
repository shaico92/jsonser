// The demo's data model: a small game world that touches every jsonser feature.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <jsonser/jsonser.hpp>

namespace demo {

enum class Role : std::uint8_t { warrior, mage, rogue, healer };  // enum -> integer

struct Vec3 {  // custom JSON shape: [x, y, z]  (see the serializer below)
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};

struct Item {  // the plain intrusive form
    std::string id;
    int qty = 0;
    double weight = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(qty) & JSONSER_NVP(weight);
    }
    bool operator==(const Item&) const = default;
};

struct Pet {  // NON-intrusive: serialize() is a free function below
    std::string name;
    std::string species;
    int level = 1;
    bool operator==(const Pet&) const = default;
};
template <class Ar>
void serialize(Ar& ar, Pet& p, unsigned) {
    ar & jsonser::make_nvp("name", p.name) & jsonser::make_nvp("species", p.species) &
        jsonser::make_nvp("level", p.level);
}

// std::variant alternatives with registered names -> {"i":1,"t":"Online","v":{...}}
struct Offline {
    std::int64_t last_seen = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(last_seen);
    }
    bool operator==(const Offline&) const = default;
};
struct Online {
    std::string server;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(server);
    }
    bool operator==(const Online&) const = default;
};
struct Banned {
    std::string reason;
    std::int64_t until = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(reason) & JSONSER_NVP(until);
    }
    bool operator==(const Banned&) const = default;
};
using Status = std::variant<Offline, Online, Banned>;

// Player, current layout = version 2.
//   v1 had: id, name, role, level, hp (int 0..100), pos, inventory
//   v2:     hp became health (float 0..1); added pet, status, skills, avatar
struct Player {
    std::uint64_t id = 0;
    std::string name;
    Role role = Role::warrior;
    int level = 1;
    float health = 1.0f;
    Vec3 pos;
    std::vector<Item> inventory;
    std::optional<Pet> pet;
    Status status;
    std::map<std::string, int> skills;
    std::vector<std::byte> avatar;  // binary -> base64 string

    template <class Ar>
    void serialize(Ar& ar, unsigned version) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(name) & JSONSER_NVP(role) & JSONSER_NVP(level);
        if (version < 2) {  // reading a v1 file: convert the old field
            int hp = 100;
            ar & JSONSER_NVP(hp);
            if constexpr (Ar::is_loading) health = static_cast<float>(hp) / 100.0f;
        } else {
            ar & JSONSER_NVP(health);
        }
        ar & JSONSER_NVP(pos) & JSONSER_NVP(inventory);
        if (version >= 2) ar & JSONSER_NVP(pet) & JSONSER_NVP(status) & JSONSER_NVP(skills) & JSONSER_NVP(avatar);
    }
    bool operator==(const Player&) const = default;
};

// The OLD program's Player (version 1), kept to demonstrate migration both ways.
struct PlayerV1 {
    std::uint64_t id = 0;
    std::string name;
    Role role = Role::warrior;
    int level = 1;
    int hp = 100;
    Vec3 pos;
    std::vector<Item> inventory;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(name) & JSONSER_NVP(role) & JSONSER_NVP(level) & JSONSER_NVP(hp) &
            JSONSER_NVP(pos) & JSONSER_NVP(inventory);
    }
};

enum class Difficulty { easy, normal, hard };

struct Settings {  // split save/load: difficulty is a readable string in the file
    std::string world_name;
    std::uint32_t seed = 0;
    double gravity = 9.81;
    bool pvp = false;
    Difficulty difficulty = Difficulty::normal;
    std::array<int, 3> spawn{};

    template <class Ar>
    void save(Ar& ar, unsigned) const {
        static const char* names[] = {"easy", "normal", "hard"};
        std::string diff = names[static_cast<int>(difficulty)];
        ar & JSONSER_NVP(world_name) & JSONSER_NVP(seed) & JSONSER_NVP(gravity) & JSONSER_NVP(pvp) &
            jsonser::make_nvp("difficulty", diff) & JSONSER_NVP(spawn);
    }
    template <class Ar>
    void load(Ar& ar, unsigned) {
        std::string diff = "normal";
        ar & JSONSER_NVP(world_name) & JSONSER_NVP(seed) & JSONSER_NVP(gravity) & JSONSER_NVP(pvp) &
            jsonser::make_nvp("difficulty", diff) & JSONSER_NVP(spawn);
        difficulty = diff == "easy" ? Difficulty::easy : diff == "hard" ? Difficulty::hard : Difficulty::normal;
    }
    JSONSER_SPLIT_MEMBER()
    bool operator==(const Settings&) const = default;
};

struct Region {
    std::string name;
    std::vector<Vec3> polygon;
    std::set<std::string> tags;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(name) & JSONSER_NVP(polygon) & JSONSER_NVP(tags);
    }
    bool operator==(const Region&) const = default;
};

// Polymorphic entities held by shared_ptr<Entity>: {"$id":3,"$type":"Monster",...}
struct Entity {
    virtual ~Entity() = default;
    std::uint64_t id = 0;
    Vec3 pos;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(pos);
    }
    virtual const char* kind() const { return "Entity"; }
};
struct Npc : Entity {
    std::string dialog;
    std::vector<std::string> quests;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Entity>(*this) & JSONSER_NVP(dialog) & JSONSER_NVP(quests);
    }
    const char* kind() const override { return "Npc"; }
};
struct Monster : Entity {
    int hp = 10;
    float aggro = 0.5f;
    std::shared_ptr<Entity> target;  // points at another entity in the list -> {"$ref": n}
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Entity>(*this) & JSONSER_NVP(hp) & JSONSER_NVP(aggro) & JSONSER_NVP(target);
    }
    const char* kind() const override { return "Monster"; }
};
struct Chest : Entity {
    std::vector<Item> loot;
    std::unique_ptr<Item> trap;  // null or an object
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & jsonser::base_object<Entity>(*this) & JSONSER_NVP(loot) & JSONSER_NVP(trap);
    }
    const char* kind() const override { return "Chest"; }
};

struct World {
    Settings settings;
    std::map<int, Region> regions;                  // int keys -> {"0": {...}, "1": {...}}
    std::vector<std::shared_ptr<Entity>> entities;  // polymorphic + shared
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(settings) & JSONSER_NVP(regions) & JSONSER_NVP(entities);
    }
};

/// Written next to the data so `load` knows how to re-generate and check it.
struct Meta {
    std::string app = "jsonser_demo";
    std::uint64_t seed = 0;
    std::uint64_t players = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(app) & JSONSER_NVP(seed) & JSONSER_NVP(players);
    }
};

}  // namespace demo

// Custom JSON shape for Vec3: [x, y, z] is what other tools expect for a vector.
template <>
struct jsonser::serializer<demo::Vec3> {
    template <class Ar>
    static void save(Ar& ar, const demo::Vec3& v) {
        ar.out().begin_array();
        ar.save_value(v.x);
        ar.save_value(v.y);
        ar.save_value(v.z);
        ar.out().end_array();
    }
    template <class Ar>
    static void load(Ar& ar, demo::Vec3& v) {
        const auto& n = ar.node();
        if (!n.is_array() || n.size() != 3) ar.fail("expected [x, y, z]");
        ar.load_element(n[0], v.x, 0);
        ar.load_element(n[1], v.y, 1);
        ar.load_element(n[2], v.z, 2);
    }
};

// Stable names in the "types" table (and in "$type"), independent of C++ namespaces.
JSONSER_CLASS_NAME_VERSION(demo::Player, "Player", 2)
JSONSER_CLASS_NAME_VERSION(demo::PlayerV1, "Player", 1)
JSONSER_CLASS_NAME(demo::Item, "Item")
JSONSER_CLASS_NAME(demo::Offline, "Offline")
JSONSER_CLASS_NAME(demo::Online, "Online")
JSONSER_CLASS_NAME(demo::Banned, "Banned")
JSONSER_CLASS_NAME(demo::Settings, "Settings")
JSONSER_CLASS_NAME(demo::World, "World")
JSONSER_EXPORT_NAMED(demo::Npc, demo::Entity, "Npc")
JSONSER_EXPORT_NAMED(demo::Monster, demo::Entity, "Monster")
JSONSER_EXPORT_NAMED(demo::Chest, demo::Entity, "Chest")
