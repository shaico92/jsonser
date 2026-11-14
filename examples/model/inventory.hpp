// A class that OWNS raw pointers, and how to serialize it safely.
#pragma once

#include <utility>
#include <vector>

#include <jsonser/jsonser.hpp>

#include "item.hpp"

namespace model {

// Holds items as raw Item* (some may point to a Weapon) and deletes them itself.
// Raw pointers work like in Boost:
//   - saving writes each object the first time as {"$id":n, ...} (and {"$ref":n} if seen again)
//   - LOADING ALLOCATES every object with `new` - so this class must delete them.
// Because it owns memory, it can be moved but not copied (copying would delete twice).
class Inventory {
public:
    Inventory() = default;
    ~Inventory() { clear(); }
    Inventory(const Inventory&) = delete;
    Inventory& operator=(const Inventory&) = delete;
    Inventory(Inventory&& other) noexcept : items_(std::exchange(other.items_, {})) {}
    Inventory& operator=(Inventory&& other) noexcept {
        if (this != &other) {
            clear();
            items_ = std::exchange(other.items_, {});
        }
        return *this;
    }

    void add(Item* item) { items_.push_back(item); }  // takes ownership
    const std::vector<Item*>& items() const { return items_; }

    // JSON: {"items":[{"$id":1,"id":"apple","qty":3}, {"$id":2,"$type":"Weapon",...}]}
    template <class Ar>
    void json_serialize(Ar& ar, unsigned /*version*/) {
        if constexpr (Ar::is_loading::value)
            clear();  // loading creates NEW items - free the ones we have first
        ar & jsonser::make_nvp("items", items_);
    }

private:
    void clear() {
        for (Item* p : items_) delete p;
        items_.clear();
    }

    std::vector<Item*> items_;
};

}  // namespace model
