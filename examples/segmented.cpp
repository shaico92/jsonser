// segmented: a big data set as a segmented archive - a folder with manifest.json + segment files.
//
// What ends up on disk (folder "samples_archive"):
//   manifest.json        <- the table of contents, written LAST
//   00000_info.json      <- the "info" object
//   00001_samples.json   <- the first ~16 MB of samples (one record per line)
//   00002_samples.json   <- the next ~16 MB, and so on
//   ...                  <- plus the files of the container examples
//
// The record classes (Sample, Station) are in model/sensor.hpp.
// Usage: example_segmented [N]   (N samples, default 1,000,000)
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <jsonser/jsonser.hpp>

#include "model/sensor.hpp"

using model::Sample;
using model::Station;

using clock_type = std::chrono::steady_clock;
static double ms_between(clock_type::time_point a, clock_type::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// ============================================================================ WRITE
static void write_archive(std::uint64_t n) {
    // Open (create) the archive folder. 16u << 20 = 16 * 1024 * 1024 = 16 MB:
    // when a segment file reaches that size, the writer starts a new file.
    jsonser::segmented_writer w("samples_archive", {16u << 20});

    // An "object" entry: one value, saved into its own small file.
    w.write("info", std::map<std::string, std::string>{{"site", "lab-3"}, {"unit", "celsius"}});

    // A "stream" entry: a long list of records, written one at a time.
    // Each push() writes the record straight to the file, so memory stays small
    // no matter how many records there are (nothing is collected in a vector).
    {
        auto s = w.stream<Sample>("samples");
        for (std::uint64_t i = 0; i < n; ++i) {
            Sample rec;
            rec.t = i;                                     // the record number
            rec.value = 20.0 + double(i % 1000) / 100;     // a fake temperature, 20.00 .. 29.99
            rec.sensor = "s" + std::to_string(i % 8);      // "s0" .. "s7"
            rec.t2 = i * 2;                                // version-2 fields
            rec.t3 = i * 3;
            rec.t54 = i * 54;
            rec.t5 = i * 5;
            s.push(rec);
        }
    }  // `s` closes here and finishes its last file

    // ---- containers ----------------------------------------------------------

    // (1) A stream of records that contain a vector and a map (see Station).
    //     Each record is one line in the segment file, containers included.
    {
        auto st = w.stream<Station>("stations");
        for (int i = 0; i < 1000; ++i)
            st.push({"st-" + std::to_string(i),
                     {20.0 + i % 5, 21.5, 19.75},                // temps
                     {{"ok", 40 + i % 10}, {"error", i % 3}}});  // counts
    }

    // (2) A whole (small/medium) container as ONE object entry, in its own file.
    //     Good when you always load it all at once. The whole thing is one JSON value:
    //       units  -> ["celsius","kelvin","fahrenheit"]
    //       limits -> {"max":80,"min":-40}
    w.write("units", std::vector<std::string>{"celsius", "kelvin", "fahrenheit"});
    w.write("limits", std::map<std::string, int>{{"min", -40}, {"max", 80}});

    // (3) A HUGE container, streamed element by element.
    //     write_stream(name, container) pushes every element for you, so the data is split
    //     into segments and you can later read a single element with read_at().
    std::vector<double> readings;
    for (int i = 0; i < 100000; ++i) readings.push_back(i * 0.5);
    w.write_stream("readings", readings);  // each line: one number, e.g. 12.5

    //     A map streams as its (key, value) pairs; each line is ["key", value].
    std::map<std::string, std::uint64_t> index_of;
    for (std::uint64_t i = 0; i < 10000; ++i) index_of["sensor-" + std::to_string(i)] = i * 100;
    w.write_stream("index_of", index_of);  // each line: ["sensor-0",0]

    // close() writes manifest.json. It comes LAST: a crash before this leaves no manifest,
    // so readers know the archive is incomplete. (The destructor also closes.)
    w.close();
}

// ============================================================================ READ: samples
static void read_samples(const jsonser::segmented_reader& r, std::uint64_t n) {
    auto t0 = clock_type::now();

    // Read ALL records in order. The loop opens one segment file at a time and hands you
    // one Sample at a time, so even a huge archive never has to fit in memory.
    // Each file's size and CRC-32 checksum (from the manifest) are checked when the loop
    // reaches the end of that file; a mismatch throws jsonser::archive_error.
    double sum = 0;
    for (Sample& s : r.stream<Sample>("samples")) sum += s.value;
    auto t1 = clock_type::now();

    // Random access: fetch ONE record by its index (here the middle one).
    // The manifest says which file holds which index range, so read_at opens only that
    // file, skips lines until the wanted record, and parses just that one line. Fast.
    Sample one = r.read_at<Sample>("samples", n / 2);
    auto t2 = clock_type::now();

    // r.info() is the parsed manifest; find("samples") gives that entry, and
    // segments.size() is how many files the stream was split into.
    std::cout << n << " samples in " << r.info().find("samples")->segments.size() << " segments\n"
              << "read all " << ms_between(t0, t1) << " ms (mean " << sum / double(n) << ")\n"
              << "read_at(" << n / 2 << ") " << ms_between(t1, t2) << " ms -> " << jsonser::to_json(one) << "\n";
}

// ============================================================================ READ: containers
static void read_containers(const jsonser::segmented_reader& r) {
    // (1) Records with containers inside: the vector and map come back filled in.
    std::size_t total_temps = 0;
    for (Station& st : r.stream<Station>("stations")) total_temps += st.temps.size();
    Station st7 = r.read_at<Station>("stations", 7);  // just one record

    // (2) Whole containers: read<T>(name) returns the full container.
    auto units = r.read<std::vector<std::string>>("units");
    auto limits = r.read<std::map<std::string, int>>("limits");

    // (3a) Streamed vector: rebuild it by pushing each element back...
    std::vector<double> readings;
    for (double& v : r.stream<double>("readings")) readings.push_back(v);
    //      ...or fetch a single element without loading the rest.
    double r500 = r.read_at<double>("readings", 500);

    // (3b) Streamed map: read the (key, value) pairs and insert them.
    //      Read them as std::pair<K, V> - NOT std::pair<const K, V> (a const key can't be filled).
    std::map<std::string, std::uint64_t> index_of;
    for (auto& kv : r.stream<std::pair<std::string, std::uint64_t>>("index_of")) index_of.insert(kv);

    std::cout << "stations: " << total_temps << " temps in total; record 7 = " << jsonser::to_json(st7) << "\n"
              << "units: " << jsonser::to_json(units) << ", limits: " << jsonser::to_json(limits) << "\n"
              << "readings: " << readings.size() << " back, readings[500] = " << r500 << "\n"
              << "index_of: " << index_of.size() << " keys, index_of[\"sensor-42\"] = " << index_of["sensor-42"]
              << "\n";
}

int main(int argc, char** argv) {
    try {
        const std::uint64_t n = argc > 1 ? std::stoull(argv[1]) : 1000000;

        auto t0 = clock_type::now();
        write_archive(n);
        std::cout << "write " << ms_between(t0, clock_type::now()) << " ms\n";

        // Opening the reader only reads manifest.json (small), not the data files.
        jsonser::segmented_reader r("samples_archive");
        read_samples(r, n);
        read_containers(r);
    } catch (const std::exception& e) {
        std::cerr << "failed: " << e.what() << "\n";
        return 1;
    }
}
