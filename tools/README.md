# tools

## read_archive.py

Reads jsonser files **without C++**, using only the Python standard library (Python 3.8+). It shows that the format is plain JSON any language can read, and it is handy for checking a save from a script.

It follows [FORMAT.md](../FORMAT.md): it reads `manifest.json`, opens only the segment it needs, and for a single record skips lines instead of parsing the whole file.

```sh
python tools/read_archive.py save_dir                    # summary: entries, records, sizes, types
python tools/read_archive.py save_dir world              # print an object entry
python tools/read_archive.py save_dir players            # a stream: how many records
python tools/read_archive.py save_dir players 777777     # print one record (reads only its segment)
python tools/read_archive.py --verify save_dir           # check every file's size and CRC-32

python tools/read_archive.py world.json                  # a single-file archive: its entries
python tools/read_archive.py world.json players          # print one top-level entry
```

`--verify` exits with status 1 if any segment's size or CRC-32 differs from the manifest, so it works in scripts and CI.

Its output for one record is byte-identical to `jsonser_demo get` (C++), which is how the cross-language reading was tested.

### Reading from your own Python code

```python
import json, os

def read_at(folder, entry_name, index):
    manifest = json.load(open(os.path.join(folder, "manifest.json"), encoding="utf-8"))
    entry = next(e for e in manifest["entries"] if e["name"] == entry_name)
    seg = [s for s in entry["segments"] if s["first_index"] <= index][-1]
    with open(os.path.join(folder, seg["file"]), "rb") as f:
        f.readline()                                   # the "[" line
        for _ in range(index - seg["first_index"]):
            f.readline()                               # skip records, one per line
        return json.loads(f.readline().rstrip(b",\r\n"))
```
