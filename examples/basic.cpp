// basic: save a map of players to one JSON file and load it back.
//
// The classes live in model/ (one header per class):
//   model/item.hpp       Item, and Weapon derived from it
//   model/inventory.hpp  Inventory - owns raw Item* pointers
//   model/player.hpp     Player - enum, optional, map, nested Inventory, version 3
#include <fstream>
#include <iostream>
#include <unordered_map>

#include <jsonser/jsonser.hpp>

#include "model/player.hpp"

using model::Item;
using model::Player;
using model::Weapon;

// Builds a few example players.
static std::unordered_map<int, Player> make_players() {
    std::unordered_map<int, Player> players;
    for (int i = 0; i < 3; ++i) {
        Player p;
        p.name = "player-" + std::to_string(i);
        p.hp = 80 + i;
        p.role = static_cast<model::Role>(i % 3);
        p.inventory.add(new Item{"apple", 3 + i});    // a plain Item
        p.inventory.add(new Weapon{"sword", 10 + i});  // a Weapon held as an Item*
        if (i != 1) p.speed = 1.5f;                     // player-1 has no speed -> "spd": null
        p.stats = {{"str", 5 + i}, {"int", 3}};
        players.emplace(i, std::move(p));               // Player owns memory: move, don't copy
    }
    return players;
}

// Save: open a file, make an output archive, write named values with <<.
static void save(const std::unordered_map<int, Player>& players, const char* file) {
    std::ofstream os(file);
    jsonser::json_oarchive oa(os, {2});  // {2} = pretty-print with 2-space indent; omit for compact
    oa << JSONSER_NVP(players);          // the name in the file is "players"
}  // the archive's destructor finishes the file (writes the "types" table)

// Load: same name, same type. Throws jsonser::archive_error (with the JSON path) on bad data.
static std::unordered_map<int, Player> load(const char* file) {
    std::unordered_map<int, Player> players;
    std::ifstream is(file);
    jsonser::json_iarchive ia(is);
    ia >> JSONSER_NVP(players);
    return players;
}

int main() {
    try {
        save(make_players(), "player.json");
        auto back = load("player.json");

        std::cout << "wrote player.json and read back " << back.size() << " players\n";
        for (int id = 0; id < static_cast<int>(back.size()); ++id) {
            const Player& p = back.at(id);
            std::cout << "  " << id << ": " << p.name << ", " << p.inventory.items().size() << " items";
            // The Item* that pointed to a Weapon is a Weapon again after loading:
            for (const Item* item : p.inventory.items())
                if (auto* w = dynamic_cast<const Weapon*>(item)) std::cout << ", weapon damage " << w->damage;
            std::cout << "\n";
        }
        std::cout << "player 0 as JSON: " << jsonser::to_json(back.at(0)) << "\n";
    } catch (const jsonser::archive_error& e) {
        std::cerr << "failed: " << e.what() << "\n";
        return 1;
    }
}
