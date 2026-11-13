// jsonser - std containers.
//   vector/deque/list/set/multiset/unordered_(multi)set, std::array, T[N]  -> JSON array
//   map/unordered_map with string, integer or enum keys                   -> JSON object
//   any other map, and every multimap                                      -> [[key, value], ...]
#pragma once

#include <array>
#include <charconv>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core.hpp"
#include "../iarchive.hpp"
#include "../oarchive.hpp"
#include "binary.hpp"  // vector<std::byte> is base64 everywhere, never an array (keeps all TUs consistent)

namespace jsonser {
namespace detail {

template <class C, class = void>
struct has_push_back : std::false_type {};
template <class C>
struct has_push_back<C, std::void_t<decltype(std::declval<C&>().push_back(std::declval<typename C::value_type>()))>>
    : std::true_type {};

template <class C, class = void>
struct has_reserve : std::false_type {};
template <class C>
struct has_reserve<C, std::void_t<decltype(std::declval<C&>().reserve(std::size_t{}))>> : std::true_type {};

template <class Ar>
const auto& expect_array(Ar& ar) {
    const auto& n = ar.node();
    if (!n.is_array()) ar.fail(std::string("expected an array, got ") + json_type_name(n));
    return n;
}

template <class C>
struct sequence_serializer {
    using V = typename C::value_type;
    template <class Ar>
    static void save(Ar& ar, const C& c) {
        auto& w = ar.out();
        w.begin_array();
        for (auto&& e : c) ar.save_value(static_cast<const V&>(e));  // cast also handles vector<bool>
        w.end_array();
    }
    template <class Ar>
    static void load(Ar& ar, C& c) {
        const auto& n = expect_array(ar);
        c.clear();
        constexpr bool in_place = std::is_same_v<C, std::vector<V>> || std::is_same_v<C, std::deque<V>>;
        if constexpr (in_place && !std::is_same_v<V, bool>) {
            c.resize(n.size());
            std::size_t i = 0;
            for (const auto& e : n) {
                ar.load_element(e, c[i], i);
                ++i;
            }
        } else {
            if constexpr (has_reserve<C>::value) c.reserve(n.size());
            std::size_t i = 0;
            for (const auto& e : n) {
                V tmp{};
                ar.load_element(e, tmp, i++);
                if constexpr (has_push_back<C>::value)
                    c.push_back(std::move(tmp));
                else
                    c.insert(std::move(tmp));
            }
        }
    }
};

template <class T, std::size_t N>
struct fixed_array_serializer {
    template <class Ar, class A>
    static void save(Ar& ar, const A& a) {
        auto& w = ar.out();
        w.begin_array();
        for (std::size_t i = 0; i < N; ++i) ar.save_value(a[i]);
        w.end_array();
    }
    template <class Ar, class A>
    static void load(Ar& ar, A& a) {
        const auto& n = expect_array(ar);
        if (n.size() != N) ar.fail("expected " + std::to_string(N) + " elements, got " + std::to_string(n.size()));
        for (std::size_t i = 0; i < N; ++i) ar.load_element(n[i], a[i], i);
    }
};

template <class K>
inline constexpr bool is_object_key =
    std::is_same_v<K, std::string> || (std::is_integral_v<K> && !std::is_same_v<K, bool>) || std::is_enum_v<K>;

template <class K>
std::string key_to_string(const K& k) {
    if constexpr (std::is_same_v<K, std::string>) {
        return k;
    } else if constexpr (std::is_enum_v<K>) {
        return key_to_string(static_cast<std::underlying_type_t<K>>(k));
    } else {
        char tmp[24];
        auto r = std::to_chars(tmp, tmp + sizeof tmp, k);
        return std::string(tmp, r.ptr);
    }
}

template <class Ar, class K>
K key_from_string(Ar& ar, const std::string& s) {
    if constexpr (std::is_same_v<K, std::string>) {
        return s;
    } else if constexpr (std::is_enum_v<K>) {
        return static_cast<K>(key_from_string<Ar, std::underlying_type_t<K>>(ar, s));
    } else {
        K k{};
        auto r = std::from_chars(s.data(), s.data() + s.size(), k);
        if (r.ec != std::errc() || r.ptr != s.data() + s.size()) ar.fail("map key '" + s + "' is not a valid integer");
        return k;
    }
}

template <class M, bool Multi>
struct map_serializer {
    using K = typename M::key_type;
    using V = typename M::mapped_type;
    static constexpr bool as_object = is_object_key<K> && !Multi;

    template <class Ar>
    static void save(Ar& ar, const M& m) {
        auto& w = ar.out();
        if constexpr (as_object) {
            w.begin_object();
            for (const auto& [k, v] : m) {
                if constexpr (std::is_same_v<K, std::string>)
                    w.key(k);
                else
                    w.key(key_to_string(k));
                ar.save_value(v);
            }
            w.end_object();
        } else {
            w.begin_array();
            for (const auto& [k, v] : m) {
                w.begin_array();
                ar.save_value(k);
                ar.save_value(v);
                w.end_array();
            }
            w.end_array();
        }
    }

    template <class Ar>
    static void load(Ar& ar, M& m) {
        const auto& n = ar.node();
        m.clear();
        if (n.is_object()) {  // {"key": value, ...}
            if constexpr (is_object_key<K>) {
                for (auto it = n.begin(); it != n.end(); ++it) {
                    V v{};
                    ar.load_child(it.value(), v, it.key());
                    m.emplace(key_from_string<Ar, K>(ar, it.key()), std::move(v));
                }
            } else {
                ar.fail("this map's key type cannot come from a JSON object key; expected [[key, value], ...]");
            }
        } else if (n.is_array()) {  // [[key, value], ...]
            std::size_t i = 0;
            for (const auto& e : n) {
                if (!e.is_array() || e.size() != 2) ar.fail("map element " + std::to_string(i) + " is not [key, value]");
                K k{};
                V v{};
                ar.load_element(e[0], k, i);
                ar.load_element(e[1], v, i);
                m.emplace(std::move(k), std::move(v));
                ++i;
            }
        } else {
            ar.fail(std::string("expected an object or an array of [key, value], got ") + json_type_name(n));
        }
    }
};

}  // namespace detail

template <class T, class A>
struct serializer<std::vector<T, A>> : detail::sequence_serializer<std::vector<T, A>> {};
template <class T, class A>
struct serializer<std::deque<T, A>> : detail::sequence_serializer<std::deque<T, A>> {};
template <class T, class A>
struct serializer<std::list<T, A>> : detail::sequence_serializer<std::list<T, A>> {};
template <class T, class C, class A>
struct serializer<std::set<T, C, A>> : detail::sequence_serializer<std::set<T, C, A>> {};
template <class T, class C, class A>
struct serializer<std::multiset<T, C, A>> : detail::sequence_serializer<std::multiset<T, C, A>> {};
template <class T, class H, class E, class A>
struct serializer<std::unordered_set<T, H, E, A>> : detail::sequence_serializer<std::unordered_set<T, H, E, A>> {};
template <class T, class H, class E, class A>
struct serializer<std::unordered_multiset<T, H, E, A>>
    : detail::sequence_serializer<std::unordered_multiset<T, H, E, A>> {};

template <class T, std::size_t N>
struct serializer<std::array<T, N>> : detail::fixed_array_serializer<T, N> {};
template <class T, std::size_t N>
struct serializer<T[N]> : detail::fixed_array_serializer<T, N> {};

template <class K, class V, class C, class A>
struct serializer<std::map<K, V, C, A>> : detail::map_serializer<std::map<K, V, C, A>, false> {};
template <class K, class V, class C, class A>
struct serializer<std::multimap<K, V, C, A>> : detail::map_serializer<std::multimap<K, V, C, A>, true> {};
template <class K, class V, class H, class E, class A>
struct serializer<std::unordered_map<K, V, H, E, A>>
    : detail::map_serializer<std::unordered_map<K, V, H, E, A>, false> {};
template <class K, class V, class H, class E, class A>
struct serializer<std::unordered_multimap<K, V, H, E, A>>
    : detail::map_serializer<std::unordered_multimap<K, V, H, E, A>, true> {};

}  // namespace jsonser
