// jsonser - Boost.Serialization-style API for JSON, with class versioning and segmented archives.
// Umbrella header: everything. Include the individual headers to pay only for what you use.
#pragma once

#include <sstream>
#include <string>

#include "core.hpp"
#include "iarchive.hpp"
#include "oarchive.hpp"
#include "segmented.hpp"
#include "types/binary.hpp"
#include "types/containers.hpp"
#include "types/memory.hpp"
#include "types/utility.hpp"
#include "writer.hpp"

namespace jsonser {

/// The JSON of one value, with no document header (no versions table): for logs and debugging.
template <class T>
std::string to_json(const T& value, int indent = 0) {
    std::ostringstream os;
    {
        writer w(os, indent);
        type_table types;
        json_oarchive ar(w, types);
        ar.save_value(value);
        w.flush();
    }
    return os.str();
}

}  // namespace jsonser
