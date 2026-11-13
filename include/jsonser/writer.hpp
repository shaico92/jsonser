// jsonser - streaming JSON writer (no DOM): buffered, UTF-8 checked, shortest round-trip numbers.
#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <ostream>
#include <string_view>

#include "core.hpp"

namespace jsonser {

// ---------------------------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, same as zlib.crc32 / Python's zlib.crc32).
// ---------------------------------------------------------------------------------------------
namespace detail {
inline constexpr std::array<std::uint32_t, 256> crc_table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
        t[i] = c;
    }
    return t;
}();
}  // namespace detail

class crc32 {
public:
    void update(const void* data, std::size_t n) {
        auto* p = static_cast<const unsigned char*>(data);
        std::uint32_t c = state_;
        for (std::size_t i = 0; i < n; ++i) c = detail::crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
        state_ = c;
    }
    std::uint32_t value() const { return state_ ^ 0xFFFFFFFFu; }

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

namespace detail {
// Length of the valid UTF-8 sequence starting at p (p[0] >= 0x80), or 0 if invalid.
inline std::size_t utf8_seq_len(const unsigned char* p, const unsigned char* end) {
    const unsigned char c = p[0];
    auto cont = [&](std::size_t i, unsigned char lo = 0x80, unsigned char hi = 0xBF) {
        return p + i < end && p[i] >= lo && p[i] <= hi;
    };
    if (c >= 0xC2 && c <= 0xDF) return cont(1) ? 2 : 0;
    if (c == 0xE0) return cont(1, 0xA0) && cont(2) ? 3 : 0;
    if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) return cont(1) && cont(2) ? 3 : 0;
    if (c == 0xED) return cont(1, 0x80, 0x9F) && cont(2) ? 3 : 0;
    if (c == 0xF0) return cont(1, 0x90) && cont(2) && cont(3) ? 4 : 0;
    if (c >= 0xF1 && c <= 0xF3) return cont(1) && cont(2) && cont(3) ? 4 : 0;
    if (c == 0xF4) return cont(1, 0x80, 0x8F) && cont(2) && cont(3) ? 4 : 0;
    return 0;
}
}  // namespace detail

/// Writes JSON straight to a std::ostream through a 64 KB buffer. Tracks bytes written and CRC-32.
/// `indent` > 0 pretty-prints; 0 writes compact JSON (one record per line in segment streams).
class writer {
public:
    static constexpr std::size_t buffer_size = 1u << 16;

    explicit writer(std::ostream& os, int indent = 0)
        : os_(&os), buf_(new char[buffer_size]), indent_(indent < 0 ? 0 : indent) {}
    writer(const writer&) = delete;
    writer& operator=(const writer&) = delete;
    ~writer() {
        try {
            flush();
        } catch (...) {
        }
    }

    // --- structure ---
    void begin_object() { open('{'); }
    void end_object() { close('}'); }
    void begin_array() { open('['); }
    void end_array() { close(']'); }

    void key(std::string_view k) {
        if (need_comma_) put(',');
        if (indent_) newline_indent();
        write_string(k);
        put(':');
        if (indent_) put(' ');
        after_key_ = true;
        need_comma_ = false;
    }

    // --- values ---
    void value_null() {
        before_value();
        write("null", 4);
        need_comma_ = true;
    }
    void value_bool(bool b) {
        before_value();
        if (b)
            write("true", 4);
        else
            write("false", 5);
        need_comma_ = true;
    }
    void value_int(std::int64_t v) {
        before_value();
        char tmp[24];
        auto r = std::to_chars(tmp, tmp + sizeof tmp, v);
        write_small(tmp, static_cast<std::size_t>(r.ptr - tmp));
        need_comma_ = true;
    }
    void value_uint(std::uint64_t v) {
        before_value();
        char tmp[24];
        auto r = std::to_chars(tmp, tmp + sizeof tmp, v);
        write_small(tmp, static_cast<std::size_t>(r.ptr - tmp));
        need_comma_ = true;
    }
    void value_double(double v) { write_floating(v); }
    void value_float(float v) { write_floating(v); }
    void value_string(std::string_view s) {
        before_value();
        write_string(s);
        need_comma_ = true;
    }

    /// Writes bytes as-is (no separators). Used for segment framing ("[\n", ",\n").
    void raw(std::string_view s) { write(s.data(), s.size()); }
    /// Forget the separator state so the next value starts fresh (records in a stream).
    void reset_separator() {
        need_comma_ = false;
        after_key_ = false;
        depth_ = 0;
    }

    void flush() {
        if (pos_ == 0) return;
        crc_.update(buf_.get(), pos_);
        os_->write(buf_.get(), static_cast<std::streamsize>(pos_));
        flushed_ += pos_;
        pos_ = 0;
        if (!*os_) throw archive_error("jsonser: write failed (disk full or stream closed?)");
    }

    std::uint64_t bytes() const { return flushed_ + pos_; }
    /// CRC-32 of everything written so far (flushes first).
    std::uint32_t crc() {
        flush();
        return crc_.value();
    }

private:
    void put(char c) {
        if (pos_ == buffer_size) flush();
        buf_[pos_++] = c;
    }
    void write(const char* p, std::size_t n) {
        if (n > buffer_size - pos_) {
            flush();
            if (n > buffer_size) {
                crc_.update(p, n);
                os_->write(p, static_cast<std::streamsize>(n));
                flushed_ += n;
                if (!*os_) throw archive_error("jsonser: write failed (disk full or stream closed?)");
                return;
            }
        }
        std::memcpy(buf_.get() + pos_, p, n);
        pos_ += n;
    }
    void write(const unsigned char* p, std::size_t n) { write(reinterpret_cast<const char*>(p), n); }
    /// For short formatted numbers (n <= 64): never takes the large-write path.
    void write_small(const char* p, std::size_t n) {
        if (buffer_size - pos_ < n) flush();
        std::memcpy(buf_.get() + pos_, p, n);
        pos_ += n;
    }

    void newline_indent() {
        put('\n');
        for (int i = 0, n = depth_ * indent_; i < n; ++i) put(' ');
    }
    void before_value() {
        if (after_key_) {
            after_key_ = false;
            return;
        }
        if (need_comma_) put(',');
        if (indent_ && depth_ > 0) newline_indent();
    }
    void open(char c) {
        before_value();
        put(c);
        ++depth_;
        need_comma_ = false;
    }
    void close(char c) {
        --depth_;
        if (indent_ && need_comma_) newline_indent();  // need_comma_ == container is non-empty
        put(c);
        need_comma_ = true;
    }

    template <class F>
    void write_floating(F v) {
        if (!std::isfinite(v)) {  // JSON has no NaN/Inf: write them as strings
            value_string(std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf"));
            return;
        }
        before_value();
        if (v == 0 && std::signbit(v)) {
            write("-0.0", 4);
        } else {
            char tmp[64];
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
            auto r = std::to_chars(tmp, tmp + sizeof tmp, v);  // shortest round-trip form
            write_small(tmp, static_cast<std::size_t>(r.ptr - tmp));
#else
            int n = std::snprintf(tmp, sizeof tmp, std::is_same_v<F, float> ? "%.9g" : "%.17g", double(v));
            write_small(tmp, static_cast<std::size_t>(n));
#endif
        }
        need_comma_ = true;
    }

    void write_string(std::string_view s) {
        static constexpr char hex[] = "0123456789abcdef";
        put('"');
        auto* p = reinterpret_cast<const unsigned char*>(s.data());
        auto* end = p + s.size();
        auto* run = p;
        while (p < end) {
            const unsigned char c = *p;
            if (c >= 0x20 && c != '"' && c != '\\' && c < 0x80) {
                ++p;
                continue;
            }
            if (c >= 0x80) {
                std::size_t n = detail::utf8_seq_len(p, end);
                if (n == 0)
                    throw archive_error(
                        "jsonser: string is not valid UTF-8 (store binary data with jsonser::as_base64)");
                p += n;
                continue;
            }
            write(run, static_cast<std::size_t>(p - run));
            put('\\');
            switch (c) {
                case '"': put('"'); break;
                case '\\': put('\\'); break;
                case '\n': put('n'); break;
                case '\r': put('r'); break;
                case '\t': put('t'); break;
                case '\b': put('b'); break;
                case '\f': put('f'); break;
                default:
                    put('u'); put('0'); put('0');
                    put(hex[c >> 4]); put(hex[c & 15]);
            }
            run = ++p;
        }
        write(run, static_cast<std::size_t>(p - run));
        put('"');
    }

    std::ostream* os_;
    std::unique_ptr<char[]> buf_;
    std::size_t pos_ = 0;
    std::uint64_t flushed_ = 0;
    crc32 crc_;
    int indent_;
    int depth_ = 0;
    bool need_comma_ = false;
    bool after_key_ = false;
};

}  // namespace jsonser
