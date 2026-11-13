// jsonser - pair / tuple -> [a, b, ...];  optional -> value or null;
//           variant -> {"i": index, "t": "TypeName" (if registered), "v": value}
#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>

#include "../core.hpp"
#include "../iarchive.hpp"
#include "../oarchive.hpp"

namespace jsonser {

namespace detail {
template <class Tup>
struct tuple_serializer {
    static constexpr std::size_t N = std::tuple_size_v<Tup>;
    template <class Ar>
    static void save(Ar& ar, const Tup& t) {
        ar.out().begin_array();
        std::apply([&](const auto&... e) { (ar.save_value(e), ...); }, t);
        ar.out().end_array();
    }
    template <class Ar>
    static void load(Ar& ar, Tup& t) {
        const auto& n = ar.node();
        if (!n.is_array() || n.size() != N)
            ar.fail("expected an array of " + std::to_string(N) + " elements");
        load_each(ar, t, n, std::make_index_sequence<N>{});
    }
    template <class Ar, class Node, std::size_t... I>
    static void load_each(Ar& ar, Tup& t, const Node& n, std::index_sequence<I...>) {
        (ar.load_element(n[I], std::get<I>(t), I), ...);
    }
};
}  // namespace detail

template <class A, class B>
struct serializer<std::pair<A, B>> : detail::tuple_serializer<std::pair<A, B>> {};
template <class... T>
struct serializer<std::tuple<T...>> : detail::tuple_serializer<std::tuple<T...>> {};

template <class T>
struct serializer<std::optional<T>> {
    template <class Ar>
    static void save(Ar& ar, const std::optional<T>& o) {
        if (o)
            ar.save_value(*o);
        else
            ar.out().value_null();
    }
    template <class Ar>
    static void load(Ar& ar, std::optional<T>& o) {
        if (ar.node().is_null()) {
            o.reset();
            return;
        }
        if (!o) o.emplace();
        ar.load_value(*o);
    }
};

template <>
struct serializer<std::monostate> {
    template <class Ar>
    static void save(Ar& ar, const std::monostate&) {
        ar.out().value_null();
    }
    template <class Ar>
    static void load(Ar&, std::monostate&) {}
};

template <class... T>
struct serializer<std::variant<T...>> {
    using V = std::variant<T...>;
    static constexpr std::size_t N = sizeof...(T);

    template <class Ar>
    static void save(Ar& ar, const V& v) {
        if (v.valueless_by_exception()) throw archive_error("jsonser: cannot save a valueless std::variant");
        auto& w = ar.out();
        w.begin_object();
        w.key("i");
        w.value_uint(v.index());
        std::visit(
            [&](const auto& alt) {
                using A = remove_cvref_t<decltype(alt)>;
                if constexpr (has_class_name<A>()) {
                    w.key("t");
                    w.value_string(class_traits<A>::name);
                }
                w.key("v");
                ar.save_value(alt);
            },
            v);
        w.end_object();
    }

    template <class Ar>
    static void load(Ar& ar, V& v) {
        const auto& n = ar.node();
        if (!n.is_object()) ar.fail("expected a variant object {\"i\":..., \"v\":...}");
        std::size_t idx = N;
        // Prefer the registered type name: survives reordering of the alternatives.
        auto t = n.find("t");
        if (t != n.end() && t->is_string()) idx = index_of_name(t->template get_ref<const std::string&>());
        if (idx == N) {
            auto i = n.find("i");
            if (i == n.end() || !i->is_number_unsigned()) ar.fail("variant has no valid \"i\" index");
            idx = i->template get<std::size_t>();
            if (idx >= N) ar.fail("variant index " + std::to_string(idx) + " out of range");
        }
        auto val = n.find("v");
        emplace_and_load(ar, v, idx, val == n.end() ? nullptr : &*val, std::index_sequence_for<T...>{});
    }

private:
    static std::size_t index_of_name(const std::string& name) {
        std::size_t idx = N, i = 0;
        ((idx == N && has_class_name<T>() && name == class_traits<T>::name ? (void)(idx = i) : void(), ++i), ...);
        return idx;
    }
    template <class Ar, class Node, std::size_t... I>
    static void emplace_and_load(Ar& ar, V& v, std::size_t idx, const Node* val, std::index_sequence<I...>) {
        (
            [&] {
                if (idx != I) return;
                v.template emplace<I>();
                if (val) ar.load_child(*val, std::get<I>(v), "v");
            }(),
            ...);
    }
};

}  // namespace jsonser
