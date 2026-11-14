# tests

Unit tests for jsonser, run with CTest. They use a tiny built-in harness ([`check.hpp`](check.hpp): `TEST`, `CHECK`, `CHECK_EQ`, `CHECK_THROWS_WITH`), so there is no test-framework dependency.

## Run

From the repository root (needs vcpkg; see the [main README](../README.md#build)):

```sh
cmake --preset msvc && cmake --build --preset msvc && ctest --preset msvc
cmake --preset mingw && cmake --build --preset mingw && ctest --preset mingw
```

Each test file is its own program (`build/<preset>/tests/.../test_<name>`). Run one directly to see every test case listed with `[ ok ]` / `[FAIL]`.

## What is covered

| File | Covers |
|---|---|
| [`test_writer.cpp`](test_writer.cpp) | The streaming JSON writer: escaping, UTF-8 validation, shortest round-trip numbers, NaN/Inf, pretty-printing, CRC-32 and byte counts |
| [`test_archive.cpp`](test_archive.cpp) | Archives: named values, the `types` table, **versioning both ways** (old file with new code and the reverse), missing and unknown fields, split `json_save`/`json_load`, `base_object`, private members, error messages with JSON paths, Boost-style `is_loading::value` / `is_saving::value`, and **separation from Boost** (a plain `serialize()` is never called) |
| [`test_types.cpp`](test_types.cpp) | Every supported type: sequences, maps (string, int and other keys), pair/tuple/optional/variant, base64, `unique_ptr`, `shared_ptr` sharing and cycles, polymorphic pointers, **raw pointers** (sharing, cycles, polymorphism, mixing with `shared_ptr`) |
| [`test_segmented.cpp`](test_segmented.cpp) | Segmented archives: a large stream round-trip, `read_at`, segment files as JSON lines, CRC corruption detection, re-formatted segments, incomplete archives, overwriting, writer misuse, empty streams, unsafe file names, containers whole and streamed |
| [`test_examples.cpp`](test_examples.cpp) | The classes in [`examples/model/`](../examples/model) round-trip correctly, so the examples stay correct |

## Adding a test

```cpp
#include "check.hpp"

TEST(my_case) {
    CHECK(1 + 1 == 2);
    CHECK_EQ(jsonser::to_json(42), "42");
    CHECK_THROWS_WITH(do_something_bad(), "part of the expected message");
}
```

A new file `test_<name>.cpp` also needs `<name>` added to the list in [`CMakeLists.txt`](CMakeLists.txt).
