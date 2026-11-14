# jsonser archive format, version 1

This spec is for programs that read or write jsonser archives without the C++ library. Everything is UTF-8 JSON ([RFC 8259](https://www.rfc-editor.org/rfc/rfc8259)).

## 1. Single-file archive

```json
{"data": {"player": {...}, "world": {...}},
 "jsonser": {"format": 1, "types": {"Player": 2, "Item": 0}}}
```

- `data` holds one key per top-level name the program saved.
- `jsonser.format` is the format version. A reader must reject a format newer than it knows.
- `jsonser.types` maps a class name to the class version the data was written with. A class missing from the table counts as version 0. A class is listed only if the program gave it a name.
- The key order in the file is not significant.

## 2. Segmented archive (a folder)

```
save_dir/manifest.json
save_dir/00000_world.json
save_dir/00001_players.json
save_dir/00002_players.json
```

### manifest.json

```json
{
  "jsonser": {"format": 1, "layout": "segmented"},
  "created": "2026-11-13T14:03:46Z",
  "types": {"Player": 2},
  "entries": [
    {"name": "world", "kind": "object", "type": "World", "count": 1,
     "segments": [{"file": "00000_world.json", "bytes": 15185, "count": 1, "first_index": 0, "crc32": 4140041979}]},
    {"name": "players", "kind": "stream", "type": "Player", "count": 1000000,
     "segments": [{"file": "00001_players.json", "bytes": 67109166, "count": 192522, "first_index": 0, "crc32": 3245729865}, ...]}
  ]
}
```

- **Lifecycle:**
  - `manifest.json` is written last. If it is missing, the archive is incomplete.
  - Readers must ignore files the manifest doesn't list.
- **Entry fields:**
  - `type` is optional. It is the registered class name of the value or records.
- **Segment fields:**
  - `file` is a plain file name inside the folder. Readers must reject paths, `..` and drive letters.
  - `bytes` is the exact size of the file.
  - `crc32` is the CRC-32 (IEEE, the same as `zlib.crc32`) of the whole file, as an unsigned integer.
- **Entry kinds:**
  - An `object` entry is one segment whose file holds exactly one JSON value.
  - A `stream` entry is zero or more segments. Each is a JSON array that holds `count` records, and the records are numbered across segments starting at `first_index`.

### Stream segment layout

```
[
{"id":0,"name":"Ka"},
{"id":1,"name":"Ri"}
]
```

- **Layout:**
  - The first line is `[`.
  - Each record follows on its own line, as compact JSON. Every record line except the last ends with `,`.
  - The file ends with `]` and a newline.
  - JSON strings never contain a raw newline, so one line is always one record.
- **Random access:**
  - To read record `i`, find the segment with the largest `first_index <= i`.
  - Skip `i - first_index` lines after the `[` line, then parse that line without its trailing `,`.
- **Fallback:** a reader may always parse the whole file as one JSON array instead. If the file size differs from `bytes`, the file was re-formatted by another tool, and the reader must do so.

## 3. Values

| C++ | JSON |
|---|---|
| class with `json_serialize` | object `{field: value, ...}`. Base-class fields sit in the same object |
| bool / integers / floating | `true`/`false`, integer, number. NaN/Inf are the strings `"nan"`, `"inf"`, `"-inf"` |
| enum | its integer value |
| string | string |
| vector, list, set, array, ... | array |
| map with string/integer/enum keys | object, with integer keys written in decimal |
| other maps, multimaps | `[[key, value], ...]` |
| pair, tuple | `[a, b, ...]` |
| optional | value or `null` |
| variant | `{"i": index, "t": "TypeName" (optional), "v": value}` |
| byte vector | base64 string (RFC 4648; readers also accept base64url) |
| unique_ptr | `null`, the value, or for a derived class `{"$type": "Name", ...fields}` |
| shared_ptr, raw pointer | `null`; first time `{"$id": n, ["$type": "Name"], ...fields}`; later `{"$ref": n}`. A non-object pointee is `{"$id": n, "$val": value}` |

- **Number ranges:** integers are written exactly, up to 64 bits. JavaScript readers lose precision above 2^53.
- **Pointer ids:**
  - `$id` numbers are unique within one single-file archive, one object entry or one stream record.
  - A `$ref` always points to an `$id` that appeared earlier in document order.
