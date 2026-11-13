// Class versioning: an old app and a new app share files in both directions.
#include <iostream>
#include <sstream>

#include <jsonser/jsonser.hpp>

// What the OLD app has: version 1, hp is an int 0..100.
struct MonsterV1 {
    std::string kind;
    int hp = 100;
    template <class Ar>
    void serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(kind) & JSONSER_NVP(hp);
    }
};
JSONSER_CLASS_NAME_VERSION(MonsterV1, "Monster", 1)

// What the NEW app has: version 2, health is a float 0..1, plus a new field.
struct Monster {
    std::string kind;
    float health = 1.0f;
    std::vector<std::string> loot;
    template <class Ar>
    void serialize(Ar& ar, unsigned version) {
        ar & JSONSER_NVP(kind);
        if (version < 2) {  // only taken when LOADING an old file
            int hp = 100;
            ar & JSONSER_NVP(hp);
            health = hp / 100.0f;
        } else {
            ar & JSONSER_NVP(health) & JSONSER_NVP(loot);
        }
    }
};
JSONSER_CLASS_VERSION(Monster, 2)

template <class T>
std::string save(const T& v) {
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os);
        oa << jsonser::make_nvp("monster", v);
    }
    return os.str();
}
template <class T>
T load(const std::string& s) {
    std::istringstream is(s);
    jsonser::json_iarchive ia(is);
    T v{};
    ia >> jsonser::make_nvp("monster", v);
    return v;
}

int main() {
    std::string old_file = save(MonsterV1{"orc", 40});
    std::cout << "old app wrote:  " << old_file;
    Monster m = load<Monster>(old_file);
    std::cout << "new app reads:  " << jsonser::to_json(m) << "  (hp 40 -> health 0.4)\n\n";

    m.loot = {"axe"};
    std::string new_file = save(m);
    std::cout << "new app wrote:  " << new_file;
    MonsterV1 o = load<MonsterV1>(new_file);
    std::cout << "old app reads:  " << jsonser::to_json(o) << "  (unknown fields ignored, hp kept its default)\n";
}
