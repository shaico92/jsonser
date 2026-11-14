// Controlling what a type looks like in JSON.
#pragma once

#include <string>

#include <jsonser/jsonser.hpp>

// ---------------------------------------------------------------------------------------------
// 1) A type from a library you can't edit: write json_serialize() as a FREE function in its namespace.
// ---------------------------------------------------------------------------------------------
namespace thirdparty {
struct Timestamp {  // pretend this comes from someone else's header
    int year = 2000, month = 1, day = 1;
};

template <class Ar>
void json_serialize(Ar& ar, Timestamp& t, unsigned) {  // found by ADL: same namespace as Timestamp
    ar & jsonser::make_nvp("year", t.year) & jsonser::make_nvp("month", t.month) &
        jsonser::make_nvp("day", t.day);
}
}  // namespace thirdparty

// ---------------------------------------------------------------------------------------------
// 2) A free json_save()/json_load() pair, when saving and loading need different code.
//    Money is stored as a readable "12.34" string instead of a number of cents.
// ---------------------------------------------------------------------------------------------
namespace thirdparty {
struct Money {
    long long cents = 0;
    std::string currency = "EUR";
};

template <class Ar>
void json_save(Ar& ar, const Money& m, unsigned) {
    const long long abs_cents = m.cents < 0 ? -m.cents : m.cents;
    std::string cents = std::to_string(abs_cents % 100);
    std::string amount = (m.cents < 0 ? "-" : "") + std::to_string(abs_cents / 100) + "." +
                         (cents.size() < 2 ? "0" + cents : cents);  // e.g. 1234 -> "12.34"
    ar & jsonser::make_nvp("amount", amount) & jsonser::make_nvp("currency", m.currency);
}
template <class Ar>
void json_load(Ar& ar, Money& m, unsigned) {
    std::string amount = "0.00";
    ar & jsonser::make_nvp("amount", amount) & jsonser::make_nvp("currency", m.currency);
    const bool negative = !amount.empty() && amount[0] == '-';
    const std::size_t dot = amount.find('.');
    const long long whole = std::stoll(amount.substr(negative ? 1 : 0, dot - (negative ? 1 : 0)));
    const long long part = dot == std::string::npos ? 0 : std::stoll(amount.substr(dot + 1));
    m.cents = (negative ? -1 : 1) * (whole * 100 + part);  // "12.34" -> 1234
}
JSONSER_SPLIT_FREE(Money)  // generates json_serialize() that calls json_save() or json_load()
}  // namespace thirdparty

namespace model {

// ---------------------------------------------------------------------------------------------
// 3) A completely custom JSON shape: specialize jsonser::serializer<T>.
//    Vec3 becomes [x, y, z] instead of {"x":..,"y":..,"z":..}.
// ---------------------------------------------------------------------------------------------
struct Vec3 {
    double x = 0, y = 0, z = 0;
};

}  // namespace model

// The serializer for Vec3. save() writes the JSON directly; load() reads the
// parsed JSON node and reports errors with ar.fail(), which adds the JSON path to the message.
template <>
struct jsonser::serializer<model::Vec3> {
    template <class Ar>
    static void save(Ar& ar, const model::Vec3& v) {
        ar.out().begin_array();
        ar.save_value(v.x);
        ar.save_value(v.y);
        ar.save_value(v.z);
        ar.out().end_array();
    }
    template <class Ar>
    static void load(Ar& ar, model::Vec3& v) {
        const auto& n = ar.node();
        if (!n.is_array() || n.size() != 3) ar.fail("expected [x, y, z]");
        ar.load_element(n[0], v.x, 0);
        ar.load_element(n[1], v.y, 1);
        ar.load_element(n[2], v.z, 2);
    }
};

namespace model {

// ---------------------------------------------------------------------------------------------
// 4) Private members + split json_save/json_load inside the class.
//    The difficulty enum is stored as readable text ("hard") instead of a number.
// ---------------------------------------------------------------------------------------------
enum class Difficulty { easy, normal, hard };

class Settings {
public:
    Settings() = default;
    Settings(std::string world, Difficulty d, Vec3 spawn) : world_(std::move(world)), difficulty_(d), spawn_(spawn) {}
    const std::string& world() const { return world_; }
    Difficulty difficulty() const { return difficulty_; }
    const Vec3& spawn() const { return spawn_; }

private:
    friend class jsonser::access;  // lets jsonser call the private json_save()/json_load()

    template <class Ar>
    void json_save(Ar& ar, unsigned) const {
        static const char* names[] = {"easy", "normal", "hard"};
        std::string difficulty = names[static_cast<int>(difficulty_)];
        ar & jsonser::make_nvp("world", world_) & JSONSER_NVP(difficulty) & jsonser::make_nvp("spawn", spawn_);
    }
    template <class Ar>
    void json_load(Ar& ar, unsigned) {
        std::string difficulty = "normal";
        ar & jsonser::make_nvp("world", world_) & JSONSER_NVP(difficulty) & jsonser::make_nvp("spawn", spawn_);
        difficulty_ = difficulty == "easy" ? Difficulty::easy
                    : difficulty == "hard" ? Difficulty::hard
                                           : Difficulty::normal;
    }
    JSONSER_SPLIT_MEMBER()  // generates json_serialize() that calls json_save() or json_load()

    std::string world_;
    Difficulty difficulty_ = Difficulty::normal;
    Vec3 spawn_;
};

}  // namespace model

JSONSER_CLASS_NAME(model::Settings, "Settings")
