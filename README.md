# jsonser

A Boost.Serialization-style C++17 API that writes **plain JSON**. It supports class versioning and **segmented archives** for very large data.

- **Header-only.** The only dependency is [nlohmann/json](https://github.com/nlohmann/json) 3.11.3, vendored in `third_party/` and used for reading only.
- **Writing is streaming.** A buffered writer of our own emits the JSON directly, so no DOM is built and memory stays flat.
- **Any app or language can read the files.** They're ordinary JSON, specified in [FORMAT.md](FORMAT.md). [`tools/read_archive.py`](tools/read_archive.py) is a stdlib-only Python reader.

```cpp
#include <jsonser/jsonser.hpp>

struct Player {
    std::string name;
    int hp = 100;
    std::vector<Item> items;
    template <class Ar>
    void serialize(Ar& ar, unsigned version) {
        ar & JSONSER_NVP(name) & JSONSER_NVP(hp);
        if (version >= 1) ar & JSONSER_NVP(items);
    }
};
JSONSER_CLASS_VERSION(Player, 1)

std::ofstream os("save.json");
jsonser::json_oarchive oa(os);          // {2} as a 2nd argument pretty-prints
oa << JSONSER_NVP(player);

std::ifstream is("save.json");
jsonser::json_iarchive ia(is);
ia >> JSONSER_NVP(player);
```

## Coming from Boost.Serialization

| Boost | jsonser |
|---|---|
| `ar & BOOST_SERIALIZATION_NVP(x)` / `make_nvp` | `ar & JSONSER_NVP(x)` / `jsonser::make_nvp("name", x)` |
| member `serialize(Ar&, const unsigned)` | same |
| free `serialize(Ar&, T&, const unsigned)` | same; put it in **T's namespace**, where ADL finds it |
| `BOOST_SERIALIZATION_SPLIT_MEMBER()` | `JSONSER_SPLIT_MEMBER()` |
| `BOOST_SERIALIZATION_SPLIT_FREE(T)` | `JSONSER_SPLIT_FREE(T)`, in T's namespace |
| `BOOST_CLASS_VERSION(T, N)` | `JSONSER_CLASS_VERSION(T, N)` at global scope. `JSONSER_CLASS_NAME_VERSION(T, "Name", N)` sets a stable name |
| `boost::serialization::base_object<B>(*this)` | `jsonser::base_object<B>(*this)`. Base fields go into the **same** JSON object |
| `BOOST_CLASS_EXPORT(Derived)` | `JSONSER_EXPORT(Derived, Base)` / `JSONSER_EXPORT_NAMED(Derived, Base, "name")` |
| `friend class boost::serialization::access` | `friend class jsonser::access` |
| `Archive::is_saving::value` | `Ar::is_saving` (constexpr bool) |
| object tracking of `shared_ptr` | `{"$id":1,...}` the first time, then `{"$ref":1}`. Cycles work |
| raw pointers | not supported: use `unique_ptr` / `shared_ptr` |

Every value needs a name, as in Boost's XML archive, because JSON objects have keys.

## Versioning

Each named class's version is written **once**, in the archive's `types` table, so the objects themselves stay clean. Loading passes **the file's** version to `serialize`:

```cpp
template <class Ar>
void serialize(Ar& ar, unsigned version) {
    ar & JSONSER_NVP(name);
    if (version < 2) {                   // old file: convert
        int hp = 100;
        ar & JSONSER_NVP(hp);
        if constexpr (Ar::is_loading) health = hp / 100.0f;
    } else {
        ar & JSONSER_NVP(health);
    }
}
```

- Fields are looked up **by name**, so field order doesn't matter.
- A field missing from the file keeps its current value (old file, new code).
- Unknown fields are ignored (new file, old code).
- `examples/versioning.cpp` and `jsonser_demo migrate-demo` show both directions.

## Large data: segmented archives

```cpp
jsonser::segmented_writer w("save_dir", {64u << 20});   // 64 MB segments
w.write("world", world);                               // one object -> its own file
auto s = w.stream<Player>("players");                  // records -> rolling files
for (auto& p : players) s.push(p);
s.close();
w.close();                                             // writes manifest.json LAST

jsonser::segmented_reader r("save_dir");
World world = r.read<World>("world");
for (Player& p : r.stream<Player>("players")) { ... }  // one record in memory at a time
Player p = r.read_at<Player>("players", 123456);       // jumps to the right segment
```

- A stream segment is a JSON array with **one record per line**. `read_at` skips lines without parsing them, and any language can do the same.
- `manifest.json` is written last, through a temp file and a rename. A crash or exception leaves no manifest, so a half-written archive never looks valid.
- **Integrity checks:**
  - Every segment has a CRC-32 (the same as `zlib.crc32`) and a byte size in the manifest.
  - `read()` and `stream()` check them by default. A stream checks each segment when it reaches the segment's end, so the records of a corrupted segment are handed out before the error is thrown.
  - `read_at()` doesn't check by default. Pass `{true, true}` as the reader options to make it check.
- Rewriting an archive in place first removes the files the old manifest listed.
- `shared_ptr` sharing is preserved within one object entry or one stream record. Each record stands alone.

## Supported types

- **Scalars and strings:**
  - all arithmetic types
  - `std::string`, which must be UTF-8 (the writer checks)
  - enums, written as their underlying integer
- **Containers:**
  - `vector`, `deque`, `list`, `set`, `multiset`, `unordered_set`, `std::array` and C arrays become a JSON array
  - `map` / `unordered_map` with string, integer or enum keys become an object
  - other maps and multimaps become `[[k, v], ...]`
- **Utility types:**
  - `pair` / `tuple` become `[a, b]`
  - `optional` becomes the value or `null`
  - `variant` becomes `{"i":index,"t":"Name","v":value}`. The name, when registered, wins over the index, so reordering the alternatives is safe.
  - `monostate` becomes `null`
- **Binary data:** `std::vector<std::byte>` becomes a base64 string. Use `jsonser::as_base64(v)` for other byte containers.
- **Smart pointers:** `unique_ptr` and `shared_ptr`, polymorphic or not.
- **Doubles:** written in shortest round-trip form. NaN and ±Inf are written as the strings `"nan"`, `"inf"` and `"-inf"`.
- **Anything else:** specialize `jsonser::serializer<T>` to give a type its own JSON shape, for example `Vec3` as `[x, y, z]` (see `apps/jsonser_demo/world.hpp`).

Errors throw `jsonser::archive_error` with the JSON path, e.g. `data.players[3].hp: expected an integer, got a string`.

## Build

```sh
cmake -S . -B build && cmake --build build --config Release
ctest --test-dir build -C Release
```

In your own project: `add_subdirectory(jsonser)` + `target_link_libraries(app PRIVATE jsonser::jsonser)`. Set `-DJSONSER_USE_SYSTEM_NLOHMANN=ON` to use an installed nlohmann_json. Tested with MSVC 19.34 (`/W4 /permissive-`, clean) and MinGW g++ 13.2 (`-Wall -Wextra -Wpedantic`, clean).

## The demo app

`apps/jsonser_demo` is a game-world app that uses every feature.

```sh
jsonser_demo generate --players 1000000 --out save_dir   # segmented archive
jsonser_demo info save_dir                               # what the manifest says
jsonser_demo get save_dir players 777777                 # random access, prints one record
jsonser_demo load save_dir                               # reads all, checks every record
jsonser_demo save-single world.json --players 50         # one pretty file to open in an editor
jsonser_demo migrate-demo                                # v1 <-> v2 in both directions

python tools/read_archive.py save_dir players 777777     # same record, read by Python
python tools/read_archive.py --verify save_dir           # sizes + CRC-32 from Python
```

Measured on this machine with 1,000,000 players (333 MB in six 64 MB segments, MSVC Release):

| Step | Time | Throughput | Peak RAM |
|---|---|---|---|
| `generate` | 3.2 s | about 104 MB/s, including generating the data | 3.9 MB |
| `load` | 16.6 s, with every record checked | about 20 MB/s | 3.8 MB |
| `get` (one record by index) | about 10 ms | | |

The C++ and Python outputs of `get` are byte-identical.

**Performance note:** reading is about 8× slower than writing because each record is parsed into an nlohmann DOM. If load speed matters, the next step is a small arena-based reader DOM in place of nlohmann; the API stays the same.
