// The Player class: plain fields, an enum, optional, map and a nested class - and a version history.
#pragma once

#include <map>
#include <optional>
#include <string>

#include <jsonser/jsonser.hpp>

#include "inventory.hpp"

namespace model {

enum class Role { warrior = 0, mage = 1, rogue = 2 };  // saved as its number: "role":1

struct Player {
    std::string name;
    int hp = 100;
    Role role = Role::warrior;
    Inventory inventory;                 // a nested class (owns raw Item* - see inventory.hpp)
    std::optional<float> speed;          // optional -> the value, or null when empty
    std::map<std::string, int> stats;    // map with string keys -> JSON object {"str":7,"int":3}

    // `version` is the version of the DATA being read (from the file's "types" table).
    // Fields added later are read only when the file is new enough, so old files still load:
    //   version 1: name, hp, role, inventory
    //   version 2: + speed  (saved under the shorter name "spd")
    //   version 3: + stats
    template <class Ar>
    void json_serialize(Ar& ar, unsigned version) {
        ar & JSONSER_NVP(name) & JSONSER_NVP(hp) & JSONSER_NVP(role) & JSONSER_NVP(inventory);
        if (version >= 2) ar & jsonser::make_nvp("spd", speed);
        if (version >= 3) ar & JSONSER_NVP(stats);
    }
};

}  // namespace model

// The current version. It is written once per file in "types": {"Player": 3}.
JSONSER_CLASS_NAME_VERSION(model::Player, "Player", 3)
