# jsonser_demo

A command-line "game world" app built on jsonser. It writes a world with up to millions of players to disk, reads it back, fetches single records by index, and shows class versioning between an old and a new program. Every library feature is used somewhere in it, so it doubles as a reference for real code.

## Build

From the repository root (needs vcpkg; see the [main README](../../README.md#build)):

```sh
cmake --preset msvc && cmake --build --preset msvc
# the program: build/msvc/apps/jsonser_demo/Release/jsonser_demo.exe
```

The library is C++17; this app uses C++20 only for defaulted `operator==` in its checks.

## Commands

| Command | What it does |
|---|---|
| `generate [--players N] [--out DIR] [--seed S] [--segment-mb M]` | Generates a world and N players (default 100,000) from a seed and writes a **segmented archive** into DIR (default `save_dir`), with segments of M MB (default 64). Players are generated and written one at a time, so memory stays flat. Prints time, size, number of files and peak RAM. |
| `save-single FILE [--players N] [--seed S]` | Writes everything into **one pretty-printed JSON file** (default 20 players): small enough to open in an editor and see what the format looks like. |
| `load PATH` | Loads a segmented folder **or** a single file, then re-generates every record from the stored seed and checks that each loaded record is identical. Prints a summary and peak RAM. |
| `get DIR players INDEX` | **Random access**: reads player number INDEX with `read_at`, which opens only the one segment that holds it, and prints it as pretty JSON. |
| `get DIR world` | Prints the world object. |
| `info DIR` | Prints the manifest: entries, segment files, sizes, record counts, CRC-32s and class versions. |
| `migrate-demo` | Saves a `Player` with the **old v1 layout** and loads it with the current v2 code, then the reverse, and shows which fields were converted or defaulted. |

### A walk-through

```sh
jsonser_demo generate --players 1000000 --out save_dir
jsonser_demo info save_dir
jsonser_demo get save_dir players 777777
jsonser_demo load save_dir
jsonser_demo save-single world.json --players 5
jsonser_demo migrate-demo

# The same archive read from Python, without any C++:
python tools/read_archive.py save_dir players 777777   # prints the same record as `get`
python tools/read_archive.py --verify save_dir         # checks sizes and CRC-32s
```

### What `generate` writes

```
save_dir/
  manifest.json           table of contents, written last
  00000_meta.json         seed and player count (lets `load` re-check everything)
  00001_world.json        settings, regions and entities
  00002_players.json      players 0 .. ~192k, one per line
  00003_players.json      the next ~192k ...
```

Measured with 1,000,000 players (333 MB, six 64 MB segments, MSVC Release):

| Step | Time | Peak RAM |
|---|---|---|
| `generate` | 3.2 s | 3.9 MB |
| `load` (every record checked) | 16.6 s | 3.8 MB |
| `get` one record | about 10 ms | |

## The data model ([`world.hpp`](world.hpp))

| Type | jsonser feature it shows | JSON |
|---|---|---|
| `Player` (version 2) | versioning: v1 files had `hp` (int), v2 has `health` (float); converted while loading | `{"id":7,"name":"Ka","health":0.8,...}` |
| `PlayerV1` | an old program's class registered under the same name, `"Player"`, version 1 | |
| `Role` | enum → its number | `"role":2` |
| `Vec3` | custom JSON shape via a `jsonser::serializer<Vec3>` specialization | `[1.5,0,-2]` |
| `Item` | a plain member `serialize()` | `{"id":"potion","qty":3}` |
| `Pet` | **non-intrusive**: a free `serialize()` function, inside `std::optional` | `null` or `{...}` |
| `Status` | `std::variant<Offline, Online, Banned>` with registered names | `{"i":1,"t":"Online","v":{...}}` |
| `skills` | `std::map<std::string,int>` | `{"fire":3}` |
| `avatar` | `std::vector<std::byte>` → base64 | `"iVBORw0..."` |
| `Settings` | split `save()`/`load()`: the difficulty enum stored as text | `"difficulty":"hard"` |
| `Region` | `std::map<int, Region>`, `std::set`, vector of `Vec3` | `{"0":{...}}` |
| `Entity` → `Npc`, `Monster`, `Chest` | polymorphic `std::shared_ptr<Entity>` with `JSONSER_EXPORT_NAMED`; `Monster::target` points at another entity | `{"$id":3,"$type":"Monster",...}`, `{"$ref":3}` |
| `Chest::trap` | `std::unique_ptr<Item>` | `null` or `{...}` |
| `World` | the object entry holding settings, regions and entities | |

[`generate.hpp`](generate.hpp) builds all of this deterministically from a seed (the same seed gives the same world), which is how `load` can verify every record.

## Files

| File | |
|---|---|
| [`main.cpp`](main.cpp) | The commands, timing and peak-RAM measurement |
| [`world.hpp`](world.hpp) | The classes and how each one is serialized |
| [`generate.hpp`](generate.hpp) | Deterministic data generator |
