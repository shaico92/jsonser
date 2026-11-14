// Record types for the segmented example.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <jsonser/jsonser.hpp>

namespace model {

// One measurement. json_serialize() lists the fields by name; one record is written as
//   version 1: {"t":123,"value":21.23,"sensor":"s3"}
//   version 2: {"t":123,"value":21.23,"sensor":"s3","t2":246,"t3":369,"t54":6642,"t5":615}
// Records written by a version-1 program still load: the t2..t5 fields keep their default 0.
struct Sample {
    std::uint64_t t = 0;
    double value = 0;
    std::string sensor;
    std::uint64_t t2 = 0;  // added in version 2
    std::uint64_t t3 = 0;
    std::uint64_t t54 = 0;
    std::uint64_t t5 = 0;

    template <class Ar>
    void json_serialize(Ar& ar, unsigned version) {  // the same function is used for saving AND loading
        ar & JSONSER_NVP(t) & JSONSER_NVP(value) & JSONSER_NVP(sensor);
        if (version >= 2) ar & JSONSER_NVP(t2) & JSONSER_NVP(t3) & JSONSER_NVP(t54) & JSONSER_NVP(t5);
    }
};

// A record that HOLDS containers. Containers need no extra code: just name them in json_serialize().
//   std::vector<double>        -> JSON array    [21.5, 22.1, 20.9]
//   std::map<std::string, int> -> JSON object   {"error": 2, "ok": 40}  (string/number keys)
// One Station record is written as
//   {"id":"st-3","temps":[21.5,22.1],"counts":{"error":2,"ok":40}}
struct Station {
    std::string id;
    std::vector<double> temps;
    std::map<std::string, int> counts;

    template <class Ar>
    void json_serialize(Ar& ar, unsigned) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(temps) & JSONSER_NVP(counts);
    }
};

}  // namespace model

// Each class version is stored ONCE in manifest.json under "types", not in every record.
JSONSER_CLASS_NAME_VERSION(model::Sample, "Sample", 2)
JSONSER_CLASS_NAME_VERSION(model::Station, "Station", 1)
