// The smallest useful program: save a struct to a JSON file and load it back.
#include <fstream>
#include <iostream>

#include <jsonser/jsonser.hpp>

struct Item {
    std::string id;
    int qty = 0;
    template <class Ar>
    void serialize(Ar& ar, unsigned /*version*/) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(qty);
    }
};

struct Player {
    std::string name;
    int hp = 100;
    std::vector<Item> items;
    std::optional<float> speed;
    template <class Ar>
    void serialize(Ar& ar, unsigned version) {
        ar & JSONSER_NVP(name) & JSONSER_NVP(hp);
        if (version >= 1) ar & JSONSER_NVP(items);
        if (version >= 2) ar & jsonser::make_nvp("spd", speed);
    }
};
JSONSER_CLASS_VERSION(Player, 2)

int main() {
    Player p{"shai", 87, {{"sword", 1}, {"apple", 3}}, 1.5f};
    {
        std::ofstream os("player.json");
        jsonser::json_oarchive oa(os, {2});  // 2 = pretty-print
        oa << JSONSER_NVP(p);
    }  // the archive's destructor finishes the file

    Player back;
    {
        std::ifstream is("player.json");
        jsonser::json_iarchive ia(is);
        ia >> jsonser::make_nvp("p", back);
    }
    std::cout << "wrote player.json, read back: " << jsonser::to_json(back) << "\n";
}
