// jsonser - Boost.Serialization-style API that reads and writes plain JSON.
// core.hpp: name-value pairs, class traits (name + version), access, dispatch, split helpers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace jsonser {

/// Every error jsonser raises (bad JSON, wrong type, CRC mismatch, ...) is an archive_error.
struct archive_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr unsigned format_version = 1;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

namespace detail {
template <class...>
inline constexpr bool always_false = false;
struct noop {
    void operator()() const {}
};
}  // namespace detail

// ---------------------------------------------------------------------------------------------
// Name-value pair. `T` is `U&` for an lvalue (the usual case) or a small wrapper held by value
// (e.g. the result of jsonser::as_base64(v)).
// ---------------------------------------------------------------------------------------------
template <class T>
struct nvp {
    const char* name;
    T value;
};

template <class T>
nvp<T> make_nvp(const char* name, T&& value) {
    return nvp<T>{name, std::forward<T>(value)};
}

#define JSONSER_NVP(x) ::jsonser::make_nvp(#x, x)

template <class T>
struct is_nvp : std::false_type {};
template <class T>
struct is_nvp<nvp<T>> : std::true_type {};

// ---------------------------------------------------------------------------------------------
// Base class: `ar & jsonser::base_object<Base>(*this);` writes Base's fields into the SAME JSON
// object as the derived fields (flat), so other apps see one plain object.
// ---------------------------------------------------------------------------------------------
template <class Base>
struct base_ref {
    Base& value;
};

template <class Base, class Derived>
base_ref<Base> base_object(const Derived& d) {
    static_assert(std::is_base_of_v<Base, Derived>, "jsonser::base_object<Base>(d): Base is not a base of d");
    return base_ref<Base>{const_cast<Base&>(static_cast<const Base&>(d))};
}

template <class T>
struct is_base_ref : std::false_type {};
template <class T>
struct is_base_ref<base_ref<T>> : std::true_type {};

// ---------------------------------------------------------------------------------------------
// Class traits: a stable name (used in the archive's "types" table) and the current version.
// Use JSONSER_CLASS_VERSION(T, N) at GLOBAL namespace scope, like BOOST_CLASS_VERSION.
// ---------------------------------------------------------------------------------------------
template <class T>
struct class_traits {
    static constexpr unsigned version = 0;
    static constexpr const char* name = nullptr;
};

#define JSONSER_CLASS_NAME_VERSION(T, NAME, VER)                 \
    namespace jsonser {                                          \
    template <>                                                  \
    struct class_traits<T> {                                     \
        static constexpr unsigned version = (VER);               \
        static constexpr const char* name = NAME;                \
    };                                                           \
    }

#define JSONSER_CLASS_VERSION(T, VER) JSONSER_CLASS_NAME_VERSION(T, #T, VER)
#define JSONSER_CLASS_NAME(T, NAME) JSONSER_CLASS_NAME_VERSION(T, NAME, 0)

template <class T>
constexpr bool has_class_name() {
    return class_traits<T>::name != nullptr;
}

// ---------------------------------------------------------------------------------------------
// access: befriend it (`friend class jsonser::access;`) to keep serialize/save/load private.
// ---------------------------------------------------------------------------------------------
class access {
public:
    template <class Ar, class T>
    static auto serialize(Ar& ar, T& t, unsigned v) -> decltype(t.serialize(ar, v)) {
        return t.serialize(ar, v);
    }
    template <class Ar, class T>
    static auto save(Ar& ar, const T& t, unsigned v) -> decltype(t.save(ar, v)) {
        return t.save(ar, v);
    }
    template <class Ar, class T>
    static auto load(Ar& ar, T& t, unsigned v) -> decltype(t.load(ar, v)) {
        return t.load(ar, v);
    }
};

namespace detail {
template <class Ar, class T, class = void>
struct has_member_serialize : std::false_type {};
template <class Ar, class T>
struct has_member_serialize<Ar, T,
                            std::void_t<decltype(access::serialize(std::declval<Ar&>(), std::declval<T&>(), 0u))>>
    : std::true_type {};
}  // namespace detail

// Nothing named serialize/save/load may be declared in namespace jsonser: these calls must find
// the user's free functions by argument-dependent lookup.
namespace adl_detail {
template <class Ar, class T, class = void>
struct has_free_serialize : std::false_type {};
template <class Ar, class T>
struct has_free_serialize<Ar, T, std::void_t<decltype(serialize(std::declval<Ar&>(), std::declval<T&>(), 0u))>>
    : std::true_type {};

template <class Ar, class T>
void call_serialize(Ar& ar, T& t, unsigned v) {
    serialize(ar, t, v);
}

template <class Ar, class T>
void split_free(Ar& ar, T& t, unsigned v) {
    if constexpr (Ar::is_saving::value)
        save(ar, static_cast<const T&>(t), v);
    else
        load(ar, t, v);
}
}  // namespace adl_detail

template <class Ar, class T>
void invoke_serialize(Ar& ar, T& t, unsigned version) {
    if constexpr (detail::has_member_serialize<Ar, T>::value) {
        access::serialize(ar, t, version);
    } else if constexpr (adl_detail::has_free_serialize<Ar, T>::value) {
        adl_detail::call_serialize(ar, t, version);
    } else {
        static_assert(detail::always_false<T>,
                      "jsonser: this type has no serialize(). Add a member "
                      "`template<class Ar> void serialize(Ar& ar, unsigned version)` (plus "
                      "`friend class jsonser::access;` if it is private), or a free "
                      "`template<class Ar> void serialize(Ar&, T&, unsigned)` in T's namespace, or include "
                      "the jsonser/types/*.hpp header that handles this std type.");
    }
}

template <class Ar, class T>
void split_member(Ar& ar, T& t, unsigned version) {
    if constexpr (Ar::is_saving::value)
        access::save(ar, static_cast<const T&>(t), version);
    else
        access::load(ar, t, version);
}

/// Inside a class: generates serialize() that calls `save(Ar&, unsigned) const` / `load(Ar&, unsigned)`.
#define JSONSER_SPLIT_MEMBER()                                         \
    template <class JsonserAr>                                         \
    void serialize(JsonserAr& ar, const unsigned version) {            \
        ::jsonser::split_member(ar, *this, version);                   \
    }

/// In T's namespace: generates a free serialize() that calls free `save(ar, const T&, v)` / `load(ar, T&, v)`.
#define JSONSER_SPLIT_FREE(T)                                          \
    template <class JsonserAr>                                         \
    void serialize(JsonserAr& ar, T& t, const unsigned version) {      \
        ::jsonser::adl_detail::split_free(ar, t, version);             \
    }

// ---------------------------------------------------------------------------------------------
// serializer<T>: how a type maps to a JSON value. The primary template handles user classes
// (a JSON object of their NVP fields). Specialize it for a custom JSON shape (see README).
// ---------------------------------------------------------------------------------------------
template <class T, class Enable = void>
struct serializer {
    static constexpr bool is_object = true;
    template <class Ar>
    static void save(Ar& ar, const T& t) {
        ar.save_object(t);
    }
    template <class Ar>
    static void load(Ar& ar, T& t) {
        ar.load_object(t);
    }
};

namespace detail {
template <class T, class = void>
struct object_flag : std::false_type {};
template <class T>
struct object_flag<T, std::void_t<decltype(serializer<T>::is_object)>>
    : std::bool_constant<serializer<T>::is_object> {};
}  // namespace detail

/// True when T is written as a JSON object of its fields (a user class with serialize()).
template <class T>
inline constexpr bool serializes_as_object = detail::object_flag<T>::value;

// --- arithmetic ------------------------------------------------------------------------------
template <class T>
struct serializer<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
    template <class Ar>
    static void save(Ar& ar, T v) {
        auto& w = ar.out();
        if constexpr (std::is_same_v<T, bool>)
            w.value_bool(v);
        else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
            w.value_int(static_cast<std::int64_t>(v));
        else if constexpr (std::is_integral_v<T>)
            w.value_uint(static_cast<std::uint64_t>(v));
        else if constexpr (std::is_same_v<T, float>)
            w.value_float(v);
        else
            w.value_double(static_cast<double>(v));
    }
    template <class Ar>
    static void load(Ar& ar, T& v) {
        if constexpr (std::is_same_v<T, bool>)
            v = ar.read_bool();
        else if constexpr (std::is_integral_v<T>)
            v = ar.template read_integer<T>();
        else
            v = static_cast<T>(ar.read_double());
    }
};

// --- enums: written as their underlying integer -----------------------------------------------
template <class T>
struct serializer<T, std::enable_if_t<std::is_enum_v<T>>> {
    using U = std::underlying_type_t<T>;
    template <class Ar>
    static void save(Ar& ar, T v) {
        serializer<U>::save(ar, static_cast<U>(v));
    }
    template <class Ar>
    static void load(Ar& ar, T& v) {
        U u{};
        serializer<U>::load(ar, u);
        v = static_cast<T>(u);
    }
};

// --- std::string --------------------------------------------------------------------------------
template <>
struct serializer<std::string> {
    template <class Ar>
    static void save(Ar& ar, const std::string& s) {
        ar.out().value_string(s);
    }
    template <class Ar>
    static void load(Ar& ar, std::string& s) {
        s = ar.read_string();
    }
};

}  // namespace jsonser
