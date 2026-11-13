// Deterministic data: player i of seed s is always the same, so `load` can check every record
// without keeping the originals around.
#pragma once

#include <cstdint>
#include <string>

#include "world.hpp"

namespace demo {

class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed) {}
    std::uint64_t next() {  // splitmix64
        std::uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % std::uint64_t(hi - lo + 1)); }
    bool chance(int percent) { return range(1, 100) <= percent; }
    template <std::size_t N>
    const char* pick(const char* const (&arr)[N]) {
        return arr[next() % N];
    }

private:
    std::uint64_t s_;
};

inline const char* const kSyllables[] = {"ka", "ri", "mo", "an", "el", "do", "sha", "ti", "ve", "lu", "gor", "ny"};
inline const char* const kItems[] = {"sword", "shield", "apple", "potion", "bow", "arrow", "gem", "scroll", "rope"};
inline const char* const kSpecies[] = {"cat", "wolf", "owl", "dragonling", "toad"};
inline const char* const kSkills[] = {"archery", "alchemy", "stealth", "smithing", "magic", "fishing"};
inline const char* const kServers[] = {"eu-1", "eu-2", "us-east", "asia"};

inline std::string make_name(Rng& r) {
    std::string n;
    for (int i = 0, k = r.range(2, 3); i < k; ++i) n += r.pick(kSyllables);
    n[0] = static_cast<char>(n[0] - 'a' + 'A');
    return n;
}

// Values are chosen to be exact in binary (quarters, small integers) so they print nicely.
inline Vec3 make_pos(Rng& r) {
    return {static_cast<float>(r.range(-4000, 4000)) / 4, static_cast<float>(r.range(0, 400)) / 4,
            static_cast<float>(r.range(-4000, 4000)) / 4};
}

inline Item make_item(Rng& r) {
    return {r.pick(kItems), r.range(1, 20), static_cast<double>(r.range(1, 80)) / 4};
}

inline Player make_player(std::uint64_t seed, std::uint64_t i) {
    Rng r(seed * 1000003ull + i);
    Player p;
    p.id = i;
    p.name = make_name(r) + (r.chance(5) ? " \xE2\x98\x85" : "");  // a few names have a UTF-8 star
    p.role = static_cast<Role>(r.range(0, 3));
    p.level = r.range(1, 60);
    p.health = static_cast<float>(r.range(0, 100)) / 100;
    p.pos = make_pos(r);
    for (int k = 0, n = r.range(0, 6); k < n; ++k) p.inventory.push_back(make_item(r));
    if (r.chance(40)) p.pet = Pet{make_name(r), r.pick(kSpecies), r.range(1, 10)};
    switch (r.range(0, 9)) {
        case 0: p.status = Banned{"spam", 1700000000 + r.range(0, 1000000)}; break;
        case 1:
        case 2:
        case 3: p.status = Online{r.pick(kServers)}; break;
        default: p.status = Offline{1690000000 + r.range(0, 10000000)};
    }
    for (int k = 0, n = r.range(0, 3); k < n; ++k) p.skills[r.pick(kSkills)] = r.range(1, 100);
    for (int k = 0; k < 6; ++k) p.avatar.push_back(static_cast<std::byte>(r.next() & 0xFF));
    return p;
}

inline World make_world(std::uint64_t seed) {
    Rng r(seed);
    World w;
    w.settings = Settings{"Valley of " + make_name(r), static_cast<std::uint32_t>(seed), 9.75, r.chance(50),
                          static_cast<Difficulty>(r.range(0, 2)), {r.range(-50, 50), 0, r.range(-50, 50)}};
    for (int i = 0; i < 6; ++i) {
        Region reg;
        reg.name = make_name(r) + " " + (i % 2 ? "Woods" : "Hills");
        for (int k = 0, n = r.range(3, 6); k < n; ++k) reg.polygon.push_back(make_pos(r));
        reg.tags = {i % 2 ? "forest" : "hills", r.chance(30) ? "dangerous" : "safe"};
        w.regions[i * 10] = std::move(reg);
    }
    std::uint64_t next_id = 1;
    std::vector<std::shared_ptr<Npc>> npcs;
    for (int i = 0; i < 40; ++i) {
        auto n = std::make_shared<Npc>();
        n->id = next_id++;
        n->pos = make_pos(r);
        n->dialog = "Hello, I am " + make_name(r) + ".";
        if (r.chance(50)) n->quests.push_back("Find the lost " + std::string(r.pick(kItems)));
        npcs.push_back(n);
    }
    // Monsters first, so a monster's target is written in full and the NPC list entry becomes {"$ref":n}.
    for (int i = 0; i < 60; ++i) {
        auto m = std::make_shared<Monster>();
        m->id = next_id++;
        m->pos = make_pos(r);
        m->hp = r.range(5, 500);
        m->aggro = static_cast<float>(r.range(0, 4)) / 4;
        if (r.chance(60)) m->target = npcs[static_cast<std::size_t>(r.range(0, 39))];
        w.entities.push_back(m);
    }
    for (auto& n : npcs) w.entities.push_back(n);
    for (int i = 0; i < 20; ++i) {
        auto c = std::make_shared<Chest>();
        c->id = next_id++;
        c->pos = make_pos(r);
        for (int k = 0, n = r.range(1, 4); k < n; ++k) c->loot.push_back(make_item(r));
        if (r.chance(25)) c->trap = std::make_unique<Item>(Item{"spike-trap", 1, 0});
        w.entities.push_back(c);
    }
    return w;
}

}  // namespace demo
