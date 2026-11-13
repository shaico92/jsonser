// jsonser - input archive: walks an nlohmann::json DOM, looks fields up BY NAME.
// Missing fields keep the member's current value (old file + new code); unknown fields are
// ignored (new file + old code).
#pragma once

#include <cmath>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core.hpp"

namespace jsonser {

using json = nlohmann::json;
using version_map = std::map<std::string, unsigned, std::less<>>;

namespace detail {
inline version_map read_types(const json& types_node) {
    version_map m;
    if (types_node.is_object())
        for (auto it = types_node.begin(); it != types_node.end(); ++it)
            if (it.value().is_number_unsigned()) m.emplace(it.key(), it.value().get<unsigned>());
    return m;
}
inline const char* json_type_name(const json& n) {
    switch (n.type()) {
        case json::value_t::null: return "null";
        case json::value_t::object: return "an object";
        case json::value_t::array: return "an array";
        case json::value_t::string: return "a string";
        case json::value_t::boolean: return "a boolean";
        case json::value_t::number_integer:
        case json::value_t::number_unsigned: return "an integer";
        case json::value_t::number_float: return "a number";
        default: return "something else";
    }
}
}  // namespace detail

class json_iarchive {
public:
    static constexpr bool is_saving = false;
    static constexpr bool is_loading = true;

    /// Document mode: parses the whole stream (a file written by json_oarchive).
    explicit json_iarchive(std::istream& is) {
        try {
            own_doc_ = json::parse(is);
        } catch (const json::exception& e) {
            throw archive_error(std::string("jsonser: invalid JSON: ") + e.what());
        }
        init_document();
    }

    /// Document mode from an already parsed DOM (kept by reference: must outlive the archive).
    explicit json_iarchive(const json& doc) : doc_(&doc) { init_document(); }

    /// Fragment mode: `root` is one value; versions come from `types` (e.g. a segment manifest).
    json_iarchive(const json& root, const version_map& types, std::string root_path = "$")
        : types_(&types), root_name_(std::move(root_path)) {
        stack_.push_back(&root);
        path_.push_back({});
    }

    json_iarchive(const json_iarchive&) = delete;
    json_iarchive& operator=(const json_iarchive&) = delete;

    /// Point a fragment archive at a new root (reuses the archive for every stream record).
    void reset_root(const json& root, std::string root_path) {
        stack_.assign(1, &root);
        path_.assign(1, {});
        root_name_ = std::move(root_path);
        reset_tracking();
    }

    template <class T>
    json_iarchive& operator>>(T&& t) {
        return *this & std::forward<T>(t);
    }

    template <class T>
    json_iarchive& operator&(T&& t) {
        using U = remove_cvref_t<T>;
        if constexpr (is_nvp<U>::value) {
            load_field(t.name, t.value);
        } else if constexpr (is_base_ref<U>::value) {
            load_base(t.value);
        } else {
            static_assert(detail::always_false<T>,
                          "jsonser: wrap values in JSONSER_NVP(x) or jsonser::make_nvp(\"name\", x) - every "
                          "JSON field needs a name");
        }
        return *this;
    }

    /// True if the current object has a field `name` (for optional logic in load()).
    bool has_field(std::string_view name) const {
        const json& n = node();
        return n.is_object() && n.find(name) != n.end();
    }

    // ---- API for serializer<T> specializations -------------------------------------------
    const json& node() const { return *stack_.back(); }

    template <class T>
    void load_value(T& v) {
        serializer<std::remove_cv_t<T>>::load(*this, v);
    }

    /// Loads `v` from `child` (an element of the current node) with path element `[index]`.
    template <class T>
    void load_element(const json& child, T& v, std::size_t index) {
        push(child, {}, index);
        load_value(v);
        pop();
    }
    /// Loads `v` from `child` with path element `.key`.
    template <class T>
    void load_child(const json& child, T& v, std::string_view key) {
        push(child, key, 0);
        load_value(v);
        pop();
    }

    template <class T>
    void load_object(T& t) {
        if (!node().is_object()) fail(std::string("expected an object, got ") + detail::json_type_name(node()));
        invoke_serialize(*this, t, file_version<T>());
    }

    template <class B>
    void load_base(B& b) {
        invoke_serialize(*this, b, file_version<B>());
    }

    /// The version of T recorded in the archive (0 if T is unnamed or absent from the table).
    template <class T>
    unsigned file_version() {
        if constexpr (!has_class_name<T>()) {
            return 0;
        } else {
            const void* key = &class_traits<T>::version;
            auto c = version_cache_.find(key);
            if (c != version_cache_.end()) return c->second;
            auto it = types_->find(std::string_view(class_traits<T>::name));
            unsigned v = it == types_->end() ? 0u : it->second;
            version_cache_.emplace(key, v);
            return v;
        }
    }

    bool read_bool() {
        if (!node().is_boolean()) fail(std::string("expected a boolean, got ") + detail::json_type_name(node()));
        return node().get<bool>();
    }

    template <class I>
    I read_integer() {
        const json& n = node();
        using L = std::numeric_limits<I>;
        if (n.is_number_unsigned()) {
            auto u = n.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(L::max())) fail("integer " + std::to_string(u) + " out of range");
            return static_cast<I>(u);
        }
        if (n.is_number_integer()) {
            auto s = n.get<std::int64_t>();
            if constexpr (std::is_unsigned_v<I>) {
                if (s < 0) fail("negative integer " + std::to_string(s) + " for an unsigned field");
                if (static_cast<std::uint64_t>(s) > static_cast<std::uint64_t>(L::max()))
                    fail("integer " + std::to_string(s) + " out of range");
            } else {
                if (s < static_cast<std::int64_t>(L::min()) || s > static_cast<std::int64_t>(L::max()))
                    fail("integer " + std::to_string(s) + " out of range");
            }
            return static_cast<I>(s);
        }
        if (n.is_number_float()) {  // another app may write 3.0 for 3
            double d = n.get<double>();
            if (std::trunc(d) == d && d >= static_cast<double>(L::min()) && d <= static_cast<double>(L::max()))
                return static_cast<I>(d);
            fail("expected an integer, got " + std::to_string(d));
        }
        fail(std::string("expected an integer, got ") + detail::json_type_name(n));
    }

    double read_double() {
        const json& n = node();
        if (n.is_number()) return n.get<double>();
        if (n.is_string()) {
            const auto& s = n.get_ref<const std::string&>();
            if (s == "nan") return std::numeric_limits<double>::quiet_NaN();
            if (s == "inf") return std::numeric_limits<double>::infinity();
            if (s == "-inf") return -std::numeric_limits<double>::infinity();
        }
        fail(std::string("expected a number, got ") + detail::json_type_name(n));
    }

    const std::string& read_string() {
        if (!node().is_string()) fail(std::string("expected a string, got ") + detail::json_type_name(node()));
        return node().get_ref<const std::string&>();
    }

    /// Throws archive_error with the JSON path of the current value, e.g. `data.players[3].hp: ...`
    [[noreturn]] void fail(const std::string& msg) const { throw archive_error("jsonser: " + path() + ": " + msg); }

    std::string path() const {
        std::string p = root_name_;
        for (std::size_t i = 1; i < path_.size(); ++i) {
            if (path_[i].is_key) {
                p += '.';
                p.append(path_[i].key.data(), path_[i].key.size());
            } else {
                p += '[' + std::to_string(path_[i].index) + ']';
            }
        }
        return p;
    }

    // ---- shared_ptr tracking -------------------------------------------------------------
    struct tracked {
        std::shared_ptr<void> ptr;
        std::type_index type;
    };
    tracked* find_tracked(std::uint64_t id) {
        auto it = shared_.find(id);
        return it == shared_.end() ? nullptr : &it->second;
    }
    void add_tracked(std::uint64_t id, std::shared_ptr<void> p, std::type_index t) {
        shared_.insert_or_assign(id, tracked{std::move(p), t});
    }
    void reset_tracking() { shared_.clear(); }

    const version_map& types() const { return *types_; }

private:
    struct path_elem {
        std::string_view key;
        std::size_t index = 0;
        bool is_key = false;
    };

    void init_document() {
        if (!doc_) doc_ = &own_doc_;
        const json& d = *doc_;
        if (!d.is_object() || !d.contains("data") || !d["data"].is_object())
            throw archive_error("jsonser: not a jsonser document (no top-level \"data\" object)");
        if (d.contains("jsonser")) {
            const json& h = d["jsonser"];
            if (h.contains("format") && h["format"].is_number_unsigned() && h["format"].get<unsigned>() > format_version)
                throw archive_error("jsonser: archive format " + std::to_string(h["format"].get<unsigned>()) +
                                    " is newer than this library supports (" + std::to_string(format_version) + ")");
            if (h.contains("types")) own_types_ = detail::read_types(h["types"]);
        }
        types_ = &own_types_;
        document_ = true;
        root_name_ = "data";
        stack_.push_back(&d["data"]);
        path_.push_back({});
    }

    template <class T>
    void load_field(const char* name, T& v) {
        const json& n = node();
        if (!n.is_object()) fail(std::string("expected an object, got ") + detail::json_type_name(n));
        auto it = n.find(name);
        if (it == n.end()) {
            if (document_ && stack_.size() == 1) fail(std::string("no top-level entry '") + name + "'");
            return;  // missing field: keep the current value
        }
        push(*it, name, 0);
        load_value(v);
        pop();
    }

    void push(const json& n, std::string_view key, std::size_t index) {
        stack_.push_back(&n);
        path_.push_back(path_elem{key, index, key.data() != nullptr});
    }
    void pop() {
        stack_.pop_back();
        path_.pop_back();
    }

    json own_doc_;
    const json* doc_ = nullptr;
    version_map own_types_;
    const version_map* types_ = &own_types_;
    bool document_ = false;
    std::string root_name_;
    std::vector<const json*> stack_;
    std::vector<path_elem> path_;
    std::unordered_map<const void*, unsigned> version_cache_;
    std::unordered_map<std::uint64_t, tracked> shared_;
};

}  // namespace jsonser
