# jsonser examples

Small programs, one topic each, with comments on almost every line. Start with `basic`, then read whichever topic you need.

The classes the examples save live in [`model/`](model), **one header per topic**, so each `.cpp` file only shows the saving and loading.

## Build

From the repository root (needs vcpkg; see the [main README](../README.md#build)):

```sh
cmake --preset msvc && cmake --build --preset msvc
# the programs: build/msvc/examples/Release/example_<name>.exe
```

## The examples

| Program | Shows | Classes |
|---|---|---|
| [`basic.cpp`](basic.cpp) | Save a `std::unordered_map<int, Player>` to one pretty JSON file and load it back. Covers named values, enums, `optional`, `map`, a nested class, raw `Item*` pointers to a derived `Weapon`, and class versions. | [`model/player.hpp`](model/player.hpp), [`model/inventory.hpp`](model/inventory.hpp), [`model/item.hpp`](model/item.hpp) |
| [`pointers.cpp`](pointers.cpp) | `unique_ptr`, `shared_ptr` that share one object (`$id` / `$ref`), a cycle (a → b → a), and derived classes through a base pointer (`$type`). | [`model/scene.hpp`](model/scene.hpp) |
| [`custom_types.cpp`](custom_types.cpp) | Control what the JSON looks like: a free `json_serialize()` for a type you can't edit, free `json_save()`/`json_load()`, a `serializer<>` specialization (`Vec3` → `[x,y,z]`), and private members with in-class `json_save()`/`json_load()`. | [`model/custom.hpp`](model/custom.hpp) |
| [`versioning.cpp`](versioning.cpp) | An old program (v1) and a new one (v2) exchange files in **both** directions; a renamed and converted field. | in the file |
| [`segmented.cpp`](segmented.cpp) | A **segmented archive** for big data: stream 1,000,000 records into 16 MB files, read them all back, `read_at` one record, plus vectors and maps (inside records, whole, and streamed element by element). Usage: `example_segmented [N]`. | [`model/sensor.hpp`](model/sensor.hpp) |

## The classes in `model/`

| Header | Classes | What to look at |
|---|---|---|
| [`item.hpp`](model/item.hpp) | `Item`, `Weapon : Item` | The simplest `json_serialize()`; `base_object<Item>`; `JSONSER_EXPORT_NAMED` so an `Item*` can hold a `Weapon` |
| [`inventory.hpp`](model/inventory.hpp) | `Inventory` | A class that **owns raw pointers**: frees them in its destructor and before loading (loading allocates with `new`), and is move-only |
| [`player.hpp`](model/player.hpp) | `Player`, `Role` | Version 3 with a field history: fields added in v2 and v3 are read only from files that have them |
| [`scene.hpp`](model/scene.hpp) | `Shape`, `Circle`, `Rect`, `Node`, `Scene` | Smart pointers, polymorphism, cycles |
| [`custom.hpp`](model/custom.hpp) | `thirdparty::Timestamp`, `thirdparty::Money`, `model::Vec3`, `model::Settings` | Four ways to customize the JSON |
| [`sensor.hpp`](model/sensor.hpp) | `Sample` (v2), `Station` | Stream records; a record holding a vector and a map |

`tests/test_examples.cpp` round-trips every class in `model/`, so these examples stay correct as the library changes.

## Tips

- **Same name and type on both sides.** `oa << make_nvp("players", x)` must be read with `ia >> make_nvp("players", y)`, where `y` has the same type. A missing top-level name throws `no top-level entry 'name'`.
- **Catch `jsonser::archive_error`.** Its message includes the JSON path of the problem, e.g. `data.players.0.hp: expected an integer`.
- **`{2}` pretty-prints.** `json_oarchive oa(os, {2})` indents by 2 spaces. Leave it out for compact files, about 4× smaller.
