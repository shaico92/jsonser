// jsonser - binary data as a base64 string.
//   std::vector<std::byte>                       -> "base64..." automatically
//   ar & make_nvp("pixels", jsonser::as_base64(v)) for vector<uint8_t>/vector<char>/std::string
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../core.hpp"
#include "../iarchive.hpp"
#include "../oarchive.hpp"

namespace jsonser {

inline std::string base64_encode(const void* data, std::size_t n) {
    static constexpr char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto* p = static_cast<const unsigned char*>(data);
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= n; i += 3) {
        std::uint32_t v = (std::uint32_t(p[i]) << 16) | (std::uint32_t(p[i + 1]) << 8) | p[i + 2];
        out += tbl[v >> 18];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
    }
    if (std::size_t rest = n - i) {
        std::uint32_t v = std::uint32_t(p[i]) << 16;
        if (rest == 2) v |= std::uint32_t(p[i + 1]) << 8;
        out += tbl[v >> 18];
        out += tbl[(v >> 12) & 63];
        out += rest == 2 ? tbl[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

/// Decodes base64 into `out` (appends). Returns false on invalid input.
template <class Bytes>
bool base64_decode(std::string_view s, Bytes& out) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;  // also accept base64url
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    while (!s.empty() && s.back() == '=') s.remove_suffix(1);
    if (s.size() % 4 == 1) return false;
    using B = typename Bytes::value_type;
    out.reserve(out.size() + s.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v = val(c);
        if (v < 0) return false;
        acc = (acc << 6) | std::uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<B>((acc >> bits) & 0xFFu));
        }
    }
    return true;
}

/// Wraps a contiguous byte container so it is written as one base64 string.
template <class C>
struct base64_ref {
    static_assert(sizeof(typename C::value_type) == 1, "jsonser::as_base64 needs a container of 1-byte elements");
    C& bytes;
};

template <class C>
base64_ref<C> as_base64(C& bytes) {
    return base64_ref<C>{bytes};
}
template <class C>
base64_ref<C> as_base64(const C& bytes) {  // saving
    return base64_ref<C>{const_cast<C&>(bytes)};
}

template <class C>
struct serializer<base64_ref<C>> {
    template <class Ar>
    static void save(Ar& ar, const base64_ref<C>& b) {
        ar.out().value_string(base64_encode(b.bytes.data(), b.bytes.size()));
    }
    template <class Ar>
    static void load(Ar& ar, base64_ref<C>& b) {
        b.bytes.clear();
        if (!base64_decode(ar.read_string(), b.bytes)) ar.fail("invalid base64 data");
    }
};

template <class A>
struct serializer<std::vector<std::byte, A>> {
    template <class Ar>
    static void save(Ar& ar, const std::vector<std::byte, A>& v) {
        ar.out().value_string(base64_encode(v.data(), v.size()));
    }
    template <class Ar>
    static void load(Ar& ar, std::vector<std::byte, A>& v) {
        v.clear();
        if (!base64_decode(ar.read_string(), v)) ar.fail("invalid base64 data");
    }
};

}  // namespace jsonser
