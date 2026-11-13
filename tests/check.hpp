// Tiny self-contained test harness (no dependency).
#pragma once

#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace check {
struct test_case {
    const char* name;
    void (*fn)();
};
inline std::vector<test_case>& registry() {
    static std::vector<test_case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
inline bool reg(const char* name, void (*fn)()) {
    registry().push_back({name, fn});
    return true;
}
}  // namespace check

#define TEST(name)                                                  \
    static void name();                                             \
    static const bool name##_registered = check::reg(#name, name);  \
    static void name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++check::failures();                                                          \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        if (!((a) == (b))) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #a, #b); \
            ++check::failures();                                                                    \
        }                                                                                           \
    } while (0)

// Passes if `expr` throws an exception whose what() contains `needle`.
#define CHECK_THROWS_WITH(expr, needle)                                                                \
    do {                                                                                               \
        bool thrown_ = false;                                                                          \
        try {                                                                                          \
            expr;                                                                                      \
        } catch (const std::exception& e_) {                                                           \
            thrown_ = true;                                                                            \
            if (std::string(e_.what()).find(needle) == std::string::npos) {                            \
                std::fprintf(stderr, "%s:%d: wrong message: %s\n  (wanted: %s)\n", __FILE__, __LINE__, \
                             e_.what(), needle);                                                       \
                ++check::failures();                                                                   \
            }                                                                                          \
        }                                                                                              \
        if (!thrown_) {                                                                                \
            std::fprintf(stderr, "%s:%d: expected an exception: %s\n", __FILE__, __LINE__, #expr);     \
            ++check::failures();                                                                       \
        }                                                                                              \
    } while (0)

int main() {
    for (auto& t : check::registry()) {
        int before = check::failures();
        try {
            t.fn();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: unexpected exception: %s\n", t.name, e.what());
            ++check::failures();
        }
        std::printf("%s %s\n", check::failures() == before ? "[ ok ]" : "[FAIL]", t.name);
    }
    std::printf("%d failure(s)\n", check::failures());
    return check::failures() == 0 ? 0 : 1;
}
