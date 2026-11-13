#include <cmath>
#include <limits>
#include <sstream>

#include <jsonser/writer.hpp>

#include "check.hpp"

using jsonser::writer;

template <class F>
static std::string write_with(F f, int indent = 0) {
    std::ostringstream os;
    {
        writer w(os, indent);
        f(w);
    }
    return os.str();
}

TEST(crc32_matches_zlib) {
    jsonser::crc32 c;
    c.update("123456789", 9);
    CHECK_EQ(c.value(), 0xCBF43926u);  // the standard check value
}

TEST(escapes_strings) {
    auto s = write_with([](writer& w) { w.value_string("a\"b\\c\nd\te\x01"); });
    CHECK_EQ(s, "\"a\\\"b\\\\c\\nd\\te\\u0001\"");
}

TEST(passes_valid_utf8) {
    auto s = write_with([](writer& w) { w.value_string("shalom \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xF0\x9F\x98\x80"); });
    CHECK_EQ(s, "\"shalom \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xF0\x9F\x98\x80\"");
}

TEST(rejects_invalid_utf8) {
    CHECK_THROWS_WITH(write_with([](writer& w) { w.value_string("bad \xFF byte"); }), "not valid UTF-8");
    CHECK_THROWS_WITH(write_with([](writer& w) { w.value_string("cut \xE2\x82"); }), "not valid UTF-8");
    CHECK_THROWS_WITH(write_with([](writer& w) { w.value_string("overlong \xC0\xAF"); }), "not valid UTF-8");
}

TEST(numbers_shortest_round_trip) {
    auto s = write_with([](writer& w) {
        w.begin_array();
        w.value_double(0.1);
        w.value_float(0.1f);
        w.value_double(3.0);
        w.value_double(-0.0);
        w.value_int(std::numeric_limits<std::int64_t>::min());
        w.value_uint(std::numeric_limits<std::uint64_t>::max());
        w.value_double(1e300);
        w.end_array();
    });
    CHECK_EQ(s, "[0.1,0.1,3,-0.0,-9223372036854775808,18446744073709551615,1e+300]");
}

TEST(nan_and_inf_become_strings) {
    auto s = write_with([](writer& w) {
        w.begin_array();
        w.value_double(std::numeric_limits<double>::quiet_NaN());
        w.value_double(std::numeric_limits<double>::infinity());
        w.value_float(-std::numeric_limits<float>::infinity());
        w.end_array();
    });
    CHECK_EQ(s, "[\"nan\",\"inf\",\"-inf\"]");
}

TEST(compact_structure) {
    auto s = write_with([](writer& w) {
        w.begin_object();
        w.key("a");
        w.begin_array();
        w.value_int(1);
        w.begin_object();
        w.end_object();
        w.begin_array();
        w.end_array();
        w.end_array();
        w.key("b");
        w.value_null();
        w.end_object();
    });
    CHECK_EQ(s, "{\"a\":[1,{},[]],\"b\":null}");
}

TEST(pretty_structure) {
    auto s = write_with(
        [](writer& w) {
            w.begin_object();
            w.key("a");
            w.begin_array();
            w.value_int(1);
            w.value_int(2);
            w.end_array();
            w.key("e");
            w.begin_object();
            w.end_object();
            w.end_object();
        },
        2);
    CHECK_EQ(s, "{\n  \"a\": [\n    1,\n    2\n  ],\n  \"e\": {}\n}");
}

TEST(large_strings_bypass_buffer_and_bytes_count) {
    std::string big(200000, 'x');
    std::ostringstream os;
    writer w(os);
    w.value_string(big);
    CHECK_EQ(w.bytes(), big.size() + 2);
    std::uint32_t c = w.crc();
    CHECK_EQ(os.str().size(), big.size() + 2);
    jsonser::crc32 ref;
    ref.update(os.str().data(), os.str().size());
    CHECK_EQ(c, ref.value());
}
