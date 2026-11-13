// jsonser_demo - an example app for the jsonser library.
//
//   jsonser_demo generate [--players N] [--out DIR] [--seed S] [--segment-mb M]
//   jsonser_demo save-single FILE [--players N] [--seed S]
//   jsonser_demo load PATH                    (a segmented DIR or a single FILE)
//   jsonser_demo get DIR players INDEX        (random access to one record)
//   jsonser_demo get DIR world
//   jsonser_demo info DIR
//   jsonser_demo migrate-demo
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "generate.hpp"
#include "world.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace fs = std::filesystem;
using namespace demo;

namespace {

double peak_ram_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return double(pmc.PeakWorkingSetSize) / (1 << 20);
    return 0;
#else
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return double(ru.ru_maxrss) / (1 << 20);
#else
    return double(ru.ru_maxrss) / 1024;
#endif
#endif
}

struct Timer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); }
};

std::string arg_value(const std::vector<std::string>& args, const std::string& flag, const std::string& def) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == flag) return args[i + 1];
    return def;
}

std::uint64_t dir_bytes(const fs::path& dir) {
    std::uint64_t n = 0;
    for (auto& f : fs::directory_iterator(dir))
        if (f.is_regular_file()) n += f.file_size();
    return n;
}

// Same world (shared + polymorphic pointers included)? Compares the serialized forms and the
// pointer identity that "$ref" must restore.
bool same_world(const World& a, const World& b) {
    if (!(a.settings == b.settings) || !(a.regions == b.regions)) return false;
    if (jsonser::to_json(a) != jsonser::to_json(b)) return false;
    for (std::size_t i = 0; i < a.entities.size(); ++i) {
        auto* ma = dynamic_cast<Monster*>(a.entities[i].get());
        auto* mb = dynamic_cast<Monster*>(b.entities[i].get());
        if (!ma != !mb) return false;
        if (!ma || !ma->target) continue;
        // index of the target in the list must be the same, and it must be the SAME object
        auto idx = [](const World& w, const Entity* e) {
            for (std::size_t k = 0; k < w.entities.size(); ++k)
                if (w.entities[k].get() == e) return k;
            return std::size_t(-1);
        };
        if (idx(a, ma->target.get()) != idx(b, mb->target.get()) || idx(b, mb->target.get()) == std::size_t(-1))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
int cmd_generate(const std::vector<std::string>& args) {
    const std::uint64_t players = std::stoull(arg_value(args, "--players", "100000"));
    const std::uint64_t seed = std::stoull(arg_value(args, "--seed", "42"));
    const fs::path out = arg_value(args, "--out", "save_dir");
    const std::uint64_t seg_mb = std::stoull(arg_value(args, "--segment-mb", "64"));

    Timer t;
    {
        jsonser::segmented_writer w(out, {seg_mb << 20});
        w.write("meta", Meta{"jsonser_demo", seed, players});
        w.write("world", make_world(seed));
        auto s = w.stream<Player>("players");
        for (std::uint64_t i = 0; i < players; ++i) {
            s.push(make_player(seed, i));  // generated and written one at a time: memory stays flat
            if ((i + 1) % 250000 == 0) std::printf("  %llu players...\n", (unsigned long long)(i + 1));
        }
        s.close();
        w.close();
    }
    double secs = t.seconds();
    double mb = double(dir_bytes(out)) / (1 << 20);
    jsonser::segmented_reader r(out);
    std::printf("wrote %llu players to %s\n  %.1f MB in %zu files (%zu player segments)\n  %.2f s (%.0f MB/s), peak RAM %.1f MB\n",
                (unsigned long long)players, out.string().c_str(), mb, std::size_t(std::distance(fs::directory_iterator(out), fs::directory_iterator())),
                r.info().find("players")->segments.size(), secs, mb / secs, peak_ram_mb());
    return 0;
}

int cmd_save_single(const std::vector<std::string>& args) {
    if (args.size() < 2) throw std::runtime_error("usage: save-single FILE [--players N]");
    const fs::path file = args[1];
    const std::uint64_t players = std::stoull(arg_value(args, "--players", "20"));
    const std::uint64_t seed = std::stoull(arg_value(args, "--seed", "42"));
    std::vector<Player> list;
    for (std::uint64_t i = 0; i < players; ++i) list.push_back(make_player(seed, i));
    World world = make_world(seed);
    Meta meta{"jsonser_demo", seed, players};
    {
        std::ofstream os(file, std::ios::binary);
        jsonser::json_oarchive oa(os, {2});
        oa << JSONSER_NVP(meta) << JSONSER_NVP(world) << jsonser::make_nvp("players", list);
    }
    std::printf("wrote %s (%.1f KB) - open it in any editor\n", file.string().c_str(), double(fs::file_size(file)) / 1024);
    return 0;
}

int cmd_load(const std::vector<std::string>& args) {
    if (args.size() < 2) throw std::runtime_error("usage: load PATH");
    const fs::path path = args[1];
    Timer t;
    Meta meta;
    World world;
    std::uint64_t checked = 0, bad = 0;
    auto check = [&](const Player& p) {
        if (!(p == make_player(meta.seed, checked))) {
            if (++bad <= 5) std::printf("  MISMATCH at player %llu\n", (unsigned long long)checked);
        }
        ++checked;
    };
    if (fs::is_directory(path)) {
        jsonser::segmented_reader r(path);
        r.read("meta", meta);
        r.read("world", world);
        for (Player& p : r.stream<Player>("players")) check(p);  // one record in memory at a time
    } else {
        std::ifstream is(path, std::ios::binary);
        jsonser::json_iarchive ia(is);
        std::vector<Player> list;
        ia >> JSONSER_NVP(meta) >> JSONSER_NVP(world) >> jsonser::make_nvp("players", list);
        for (auto& p : list) check(p);
    }
    bool world_ok = same_world(world, make_world(meta.seed));
    std::printf("loaded %s: world %s (%zu regions, %zu entities), %llu/%llu players match, %.2f s, peak RAM %.1f MB\n",
                path.string().c_str(), world_ok ? "OK" : "MISMATCH", world.regions.size(), world.entities.size(),
                (unsigned long long)(checked - bad), (unsigned long long)meta.players, t.seconds(), peak_ram_mb());
    return world_ok && bad == 0 && checked == meta.players ? 0 : 1;
}

int cmd_get(const std::vector<std::string>& args) {
    if (args.size() < 3) throw std::runtime_error("usage: get DIR players INDEX | get DIR world");
    jsonser::segmented_reader r(args[1]);
    if (args[2] == "world") {
        std::cout << jsonser::to_json(r.read<World>("world"), 2) << "\n";
    } else if (args[2] == "players" && args.size() >= 4) {
        Timer t;
        Player p = r.read_at<Player>("players", std::stoull(args[3]));
        double ms = t.seconds() * 1000;
        std::cout << jsonser::to_json(p, 2) << "\n";
        std::fprintf(stderr, "(read_at took %.1f ms)\n", ms);
    } else {
        throw std::runtime_error("usage: get DIR players INDEX | get DIR world");
    }
    return 0;
}

int cmd_info(const std::vector<std::string>& args) {
    if (args.size() < 2) throw std::runtime_error("usage: info DIR");
    jsonser::segmented_reader r(args[1]);
    const auto& m = r.info();
    std::printf("archive %s  (format %u, created %s)\ntypes:", args[1].c_str(), m.format, m.created.c_str());
    for (auto& [name, ver] : m.types) std::printf(" %s=v%u", name.c_str(), ver);
    std::printf("\nentries:\n");
    for (auto& e : m.entries) {
        std::uint64_t bytes = 0;
        for (auto& s : e.segments) bytes += s.bytes;
        std::printf("  %-8s %-6s %-8s %10llu records %9.2f MB in %zu file(s)\n", e.name.c_str(), e.kind.c_str(),
                    e.type.empty() ? "-" : e.type.c_str(), (unsigned long long)e.count, double(bytes) / (1 << 20),
                    e.segments.size());
        if (e.segments.size() > 1)
            for (auto& s : e.segments)
                std::printf("           %s  [%llu..%llu]  crc32=%08x\n", s.file.c_str(), (unsigned long long)s.first_index,
                            (unsigned long long)(s.first_index + s.count - 1), s.crc32);
    }
    return 0;
}

template <class T>
std::string save_string(const T& v) {
    std::ostringstream os;
    {
        jsonser::json_oarchive oa(os);
        oa << jsonser::make_nvp("player", v);
    }
    return os.str();
}
template <class T>
T load_string(const std::string& s) {
    std::istringstream is(s);
    jsonser::json_iarchive ia(is);
    T v{};
    ia >> jsonser::make_nvp("player", v);
    return v;
}

int cmd_migrate_demo() {
    std::printf("== 1. The OLD program (Player v1: int hp) saves a player\n");
    PlayerV1 old;
    old.id = 7;
    old.name = "Oldtimer";
    old.role = Role::mage;
    old.level = 33;
    old.hp = 45;
    old.pos = {1, 2, 3};
    old.inventory = {{"staff", 1, 2.5}};
    std::string v1_file = save_string(old);
    std::printf("%s\n", v1_file.c_str());

    std::printf("== 2. The NEW program (Player v2) loads that v1 file\n");
    Player now = load_string<Player>(v1_file);
    std::printf("%s\n", jsonser::to_json(now, 2).c_str());
    std::printf("   hp 45 -> health %.2f (converted in serialize() because the file says Player=v1)\n", now.health);
    std::printf("   pet, status, skills, avatar were not in the file -> kept their defaults\n\n");

    std::printf("== 3. The NEW program saves; the OLD program reads the v2 file\n");
    now.pet = Pet{"Ash", "owl", 2};
    std::string v2_file = save_string(now);
    std::printf("%s\n", v2_file.c_str());
    PlayerV1 back = load_string<PlayerV1>(v2_file);
    std::printf("   old program got: %s\n", jsonser::to_json(back).c_str());
    std::printf("   unknown fields (health, pet, ...) were ignored; hp (gone from v2) kept its default %d\n", back.hp);
    return 0;
}

void usage() {
    std::printf(
        "jsonser_demo - example app for the jsonser library\n\n"
        "  generate [--players N] [--out DIR] [--seed S] [--segment-mb M]   write a segmented archive\n"
        "  save-single FILE [--players N] [--seed S]                       write one pretty JSON file\n"
        "  load PATH                                                       read DIR or FILE and verify every record\n"
        "  get DIR players INDEX | get DIR world                           random access read\n"
        "  info DIR                                                        show the manifest\n"
        "  migrate-demo                                                    class versioning, both directions\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 2;
    }
    try {
        const std::string& cmd = args[0];
        if (cmd == "generate") return cmd_generate(args);
        if (cmd == "save-single") return cmd_save_single(args);
        if (cmd == "load") return cmd_load(args);
        if (cmd == "get") return cmd_get(args);
        if (cmd == "info") return cmd_info(args);
        if (cmd == "migrate-demo") return cmd_migrate_demo();
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
