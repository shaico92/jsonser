#!/usr/bin/env python3
"""Read a jsonser archive with nothing but the Python standard library.

Proves the format is plain JSON any app can read. Usage:

    read_archive.py PATH                       summary (segmented DIR or single FILE)
    read_archive.py DIR ENTRY                  print an object entry / count a stream
    read_archive.py DIR ENTRY INDEX            print one stream record (reads only its segment)
    read_archive.py FILE NAME                  print a top-level entry of a single-file archive
    read_archive.py --verify DIR               check every segment's size and CRC-32
"""
import bisect
import json
import os
import sys
import zlib


def load_manifest(d):
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        m = json.load(f)
    if m["jsonser"]["format"] > 1:
        sys.exit("archive format is newer than this reader")
    return m


def entry(m, name):
    for e in m["entries"]:
        if e["name"] == name:
            return e
    sys.exit(f"no entry {name!r}; entries: {[e['name'] for e in m['entries']]}")


def seg_path(d, s):
    f = s["file"]
    if os.path.basename(f) != f or f in (".", ".."):
        sys.exit(f"unsafe file name in manifest: {f!r}")
    return os.path.join(d, f)


def read_at(d, e, index):
    """Find the segment from the manifest, then skip lines: one record per line."""
    if not 0 <= index < e["count"]:
        sys.exit(f"index {index} out of range (count {e['count']})")
    firsts = [s["first_index"] for s in e["segments"]]
    s = e["segments"][bisect.bisect_right(firsts, index) - 1]
    with open(seg_path(d, s), "rb") as f:
        assert f.readline().strip() == b"["
        for _ in range(index - s["first_index"]):
            f.readline()
        return json.loads(f.readline().rstrip(b",\r\n"))


def verify(d):
    m = load_manifest(d)
    ok = True
    for e in m["entries"]:
        for s in e["segments"]:
            with open(seg_path(d, s), "rb") as f:
                data = f.read()
            good = len(data) == s["bytes"] and zlib.crc32(data) == s["crc32"]
            ok &= good
            print(f"{'ok ' if good else 'BAD'} {s['file']}  {len(data)} bytes  crc32={zlib.crc32(data):08x}")
    sys.exit(0 if ok else 1)


def dump(v):
    print(json.dumps(v, indent=2, ensure_ascii=False))


def main(argv):
    if not argv:
        sys.exit(__doc__)
    if argv[0] == "--verify":
        verify(argv[1])
    path = argv[0]
    if os.path.isfile(path):  # single-file archive: {"data": {...}, "jsonser": {...}}
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        if len(argv) == 1:
            print("single-file archive; types:", doc["jsonser"]["types"])
            print("entries:", list(doc["data"]))
        else:
            dump(doc["data"][argv[1]])
        return
    m = load_manifest(path)
    if len(argv) == 1:
        print(f"segmented archive, format {m['jsonser']['format']}, created {m.get('created')}")
        print("types:", m["types"])
        for e in m["entries"]:
            size = sum(s["bytes"] for s in e["segments"])
            print(f"  {e['name']:<10} {e['kind']:<6} {e.get('type', '-'):<8} {e['count']:>10} records"
                  f"  {size / 2**20:8.2f} MB  {len(e['segments'])} file(s)")
        return
    e = entry(m, argv[1])
    if e["kind"] == "object":
        with open(seg_path(path, e["segments"][0]), encoding="utf-8") as f:
            dump(json.load(f))
    elif len(argv) >= 3:
        dump(read_at(path, e, int(argv[2])))
    else:
        print(f"{e['name']}: stream of {e['count']} records; give an INDEX to print one")


if __name__ == "__main__":
    main(sys.argv[1:])
