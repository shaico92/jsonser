// Items: a plain class, plus a derived class used through a base-class pointer.
#pragma once

#include <string>

#include <jsonser/jsonser.hpp>

namespace model {

// The simplest serializable class: list each member once, by name.
//   JSON: {"id":"apple","qty":3}
struct Item {
    std::string id;
    int qty = 0;

    Item() = default;
    Item(std::string id_, int qty_) : id(std::move(id_)), qty(qty_) {}
    virtual ~Item() = default;  // virtual: Items are also held as Item* that point to a Weapon

    template <class Ar>
    void json_serialize(Ar& ar, unsigned /*version*/) {
        ar & JSONSER_NVP(id) & JSONSER_NVP(qty);
    }
};

// A derived class. base_object<Item> writes Item's fields into the SAME JSON object.
// Through an Item* it is saved with its real type, so loading creates a Weapon again:
//   JSON: {"$id":1,"$type":"Weapon","id":"sword","qty":1,"damage":12}
struct Weapon : Item {
    int damage = 0;

    Weapon() = default;
    Weapon(std::string id_, int damage_) : Item(std::move(id_), 1), damage(damage_) {}

    template <class Ar>
    void json_serialize(Ar& ar, unsigned /*version*/) {
        ar & jsonser::base_object<Item>(*this) & JSONSER_NVP(damage);
    }
};

}  // namespace model

// Register Weapon as a kind of Item, so an Item* that points to a Weapon can be saved and loaded.
// "Weapon" is the name written as "$type" (JSONSER_EXPORT(T, Base) would write "model::Weapon").
JSONSER_EXPORT_NAMED(model::Weapon, model::Item, "Weapon")
