// jsonser - smart pointers.
//   unique_ptr<T>  -> null | the object | {"$type":"Derived", ...fields} for a registered derived class
//   shared_ptr<T>  -> null | {"$id":1, ["$type":...], ...fields} first time | {"$ref":1} after that
// Sharing is preserved inside one archive (or one stream record). A non-object T (e.g.
// shared_ptr<int>) is wrapped: {"$id":1,"$val":5}.
//
// Polymorphic pointers: register each derived class against the base you hold it through,
// at namespace scope:   JSONSER_EXPORT(Npc, Entity)   or   JSONSER_EXPORT_NAMED(Npc, Entity, "npc")
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

#include "../core.hpp"
#include "../iarchive.hpp"
#include "../oarchive.hpp"

namespace jsonser {

template <class Base>
class poly_registry {
public:
    struct entry {
        std::string name;
        void (*save)(json_oarchive&, const Base*, const std::function<void()>&);
        Base* (*create)();
        void (*load)(json_iarchive&, Base*);
    };

    static poly_registry& instance() {
        static poly_registry r;
        return r;
    }

    template <class Derived>
    void add(const char* name) {
        entry e{name,
                [](json_oarchive& ar, const Base* b, const std::function<void()>& extra) {
                    ar.save_object(*dynamic_cast<const Derived*>(b), extra);
                },
                []() -> Base* { return new Derived(); },
                [](json_iarchive& ar, Base* b) { ar.load_object(*dynamic_cast<Derived*>(b)); }};
        by_type_.insert_or_assign(std::type_index(typeid(Derived)), e);
        by_name_.insert_or_assign(e.name, e);
    }
    const entry* find(std::type_index t) const {
        auto it = by_type_.find(t);
        return it == by_type_.end() ? nullptr : &it->second;
    }
    const entry* find(const std::string& name) const {
        auto it = by_name_.find(name);
        return it == by_name_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::type_index, entry> by_type_;
    std::unordered_map<std::string, entry> by_name_;
};

template <class Derived, class Base>
bool register_polymorphic(const char* name) {
    static_assert(std::is_polymorphic_v<Base>, "JSONSER_EXPORT: Base needs a virtual function (e.g. a virtual destructor)");
    static_assert(std::is_base_of_v<Base, Derived>, "JSONSER_EXPORT(Derived, Base): Base is not a base of Derived");
    static_assert(std::is_default_constructible_v<Derived>, "JSONSER_EXPORT: Derived must be default constructible");
    poly_registry<Base>::instance().template add<Derived>(name);
    return true;
}

#define JSONSER_DETAIL_CAT2(a, b) a##b
#define JSONSER_DETAIL_CAT(a, b) JSONSER_DETAIL_CAT2(a, b)
#define JSONSER_EXPORT_NAMED(Derived, Base, NAME)                                                       \
    namespace {                                                                                         \
    [[maybe_unused]] const bool JSONSER_DETAIL_CAT(jsonser_export_, __COUNTER__) =                       \
        ::jsonser::register_polymorphic<Derived, Base>(NAME);                                           \
    }
#define JSONSER_EXPORT(Derived, Base) JSONSER_EXPORT_NAMED(Derived, Base, #Derived)

namespace detail {

// Writes *p (whose static type is T). `extra` adds $id; this adds $type for a derived object.
template <class T>
void save_pointee(json_oarchive& ar, const T* p, const std::function<void()>& id_extra) {
    if constexpr (std::is_polymorphic_v<T>) {
        if (std::type_index(typeid(*p)) != std::type_index(typeid(T))) {
            auto* e = poly_registry<T>::instance().find(std::type_index(typeid(*p)));
            if (!e)
                throw archive_error(std::string("jsonser: unregistered polymorphic type ") + typeid(*p).name() +
                                    " - add JSONSER_EXPORT(Derived, Base)");
            e->save(ar, p, [&] {
                id_extra();
                ar.out().key("$type");
                ar.out().value_string(e->name);
            });
            return;
        }
    }
    if constexpr (serializes_as_object<T>) {
        ar.save_object(*p, id_extra);
    } else {
        ar.out().begin_object();
        id_extra();
        ar.out().key("$val");
        ar.save_value(*p);
        ar.out().end_object();
    }
}

// Step 1: create the right (empty) object for the current node: "$type" or T itself.
// Returns the registry entry used (nullptr when the object is a plain T).
template <class T>
std::unique_ptr<T> create_for(json_iarchive& ar, const typename poly_registry<T>::entry*& used) {
    used = nullptr;
    const auto& n = ar.node();
    if constexpr (std::is_polymorphic_v<T>) {
        auto t = n.is_object() ? n.find("$type") : n.end();
        if (t != n.end()) {
            if (!t->is_string()) ar.fail("\"$type\" must be a string");
            used = poly_registry<T>::instance().find(t->template get_ref<const std::string&>());
            if (!used)
                ar.fail("unknown \"$type\" '" + t->template get<std::string>() +
                        "' - add JSONSER_EXPORT(Derived, Base) in this program");
            return std::unique_ptr<T>(used->create());
        }
    }
    if constexpr (std::is_abstract_v<T>) {
        ar.fail("object has no \"$type\" and its pointer type is abstract");
    } else {
        return std::make_unique<T>();
    }
}

// Step 2: load the fields into the object created by create_for.
template <class T>
void load_pointee(json_iarchive& ar, T* obj, const typename poly_registry<T>::entry* used) {
    if (used) {
        used->load(ar, obj);
    } else if constexpr (serializes_as_object<T>) {
        ar.load_value(*obj);
    } else {
        const auto& n = ar.node();
        auto v = n.is_object() ? n.find("$val") : n.end();
        if (v == n.end()) ar.fail("expected {\"$val\": ...}");
        ar.load_child(*v, *obj, "$val");
    }
}

template <class T>
const void* most_derived(const T* p) {
    if constexpr (std::is_polymorphic_v<T>)
        return dynamic_cast<const void*>(p);
    else
        return p;
}

}  // namespace detail

template <class T, class D>
struct serializer<std::unique_ptr<T, D>> {
    static void save(json_oarchive& ar, const std::unique_ptr<T, D>& p) {
        if (!p) {
            ar.out().value_null();
            return;
        }
        if constexpr (serializes_as_object<T> || std::is_polymorphic_v<T>)
            detail::save_pointee(ar, p.get(), [] {});
        else
            ar.save_value(*p);  // unique_ptr<int> -> 5
    }
    static void load(json_iarchive& ar, std::unique_ptr<T, D>& p) {
        if (ar.node().is_null()) {
            p.reset();
            return;
        }
        if constexpr (serializes_as_object<T> || std::is_polymorphic_v<T>) {
            const typename poly_registry<T>::entry* used;
            auto obj = detail::create_for<T>(ar, used);
            detail::load_pointee(ar, obj.get(), used);
            p.reset(obj.release());
        } else {
            auto obj = std::make_unique<T>();
            ar.load_value(*obj);
            p.reset(obj.release());
        }
    }
};

template <class T>
struct serializer<std::shared_ptr<T>> {
    static void save(json_oarchive& ar, const std::shared_ptr<T>& p) {
        auto& w = ar.out();
        if (!p) {
            w.value_null();
            return;
        }
        auto [id, first] = ar.track(detail::most_derived(p.get()));
        if (!first) {
            w.begin_object();
            w.key("$ref");
            w.value_uint(id);
            w.end_object();
            return;
        }
        detail::save_pointee(ar, p.get(), [&w, id = id] {
            w.key("$id");
            w.value_uint(id);
        });
    }
    static void load(json_iarchive& ar, std::shared_ptr<T>& p) {
        const auto& n = ar.node();
        if (n.is_null()) {
            p.reset();
            return;
        }
        if (!n.is_object()) ar.fail("expected a shared_ptr object, got " + std::string(detail::json_type_name(n)));
        auto ref = n.find("$ref");
        if (ref != n.end()) {
            if (!ref->is_number_unsigned()) ar.fail("\"$ref\" must be an unsigned integer");
            auto* t = ar.find_tracked(ref->template get<std::uint64_t>());
            if (!t) ar.fail("\"$ref\": " + ref->dump() + " points to no earlier \"$id\"");
            if (t->type != std::type_index(typeid(T)))
                ar.fail("\"$ref\" object was loaded through a different pointer type");
            p = std::static_pointer_cast<T>(t->ptr);
            return;
        }
        const typename poly_registry<T>::entry* used;
        p = std::shared_ptr<T>(detail::create_for<T>(ar, used));
        auto id = n.find("$id");
        if (id != n.end() && id->is_number_unsigned())  // track BEFORE loading: cycles can $ref it
            ar.add_tracked(id->template get<std::uint64_t>(), std::static_pointer_cast<void>(p),
                           std::type_index(typeid(T)));
        detail::load_pointee(ar, p.get(), used);
    }
};

template <class T>
struct serializer<T*> {
    static_assert(detail::always_false<T>,
                  "jsonser: raw pointers are not serialized - use std::unique_ptr or std::shared_ptr");
};

}  // namespace jsonser
