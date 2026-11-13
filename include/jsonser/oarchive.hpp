// jsonser - output archive: writes through jsonser::writer, never builds a DOM.
#pragma once

#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>

#include "core.hpp"
#include "writer.hpp"

namespace jsonser {

/// The class names and versions met while saving. Written once per archive (the "types" table).
class type_table {
public:
    template <class T>
    void note() {
        if constexpr (has_class_name<T>()) {
            const void* key = &class_traits<T>::version;  // unique per type across translation units
            if (seen_.find(key) == seen_.end()) seen_.emplace(key, entry{class_traits<T>::name, class_traits<T>::version});
        }
    }
    /// name -> version. Throws if two C++ types share a name with different versions.
    std::map<std::string, unsigned> to_map() const {
        std::map<std::string, unsigned> m;
        for (auto& [k, e] : seen_) {
            auto [it, inserted] = m.emplace(e.name, e.version);
            if (!inserted && it->second != e.version)
                throw archive_error(std::string("jsonser: two types are registered as '") + e.name +
                                    "' with different versions in one archive");
        }
        return m;
    }
    void merge(const type_table& o) {
        for (auto& kv : o.seen_) seen_.insert(kv);
    }

private:
    struct entry {
        const char* name;
        unsigned version;
    };
    std::unordered_map<const void*, entry> seen_;
};

namespace detail {
inline void write_types(writer& w, const std::map<std::string, unsigned>& types) {
    w.begin_object();
    for (auto& [name, ver] : types) {
        w.key(name);
        w.value_uint(ver);
    }
    w.end_object();
}
}  // namespace detail

struct oarchive_options {
    int indent = 0;  ///< 0 = compact, N = pretty-print with N spaces
};

class json_oarchive {
public:
    static constexpr bool is_saving = true;
    static constexpr bool is_loading = false;

    using options = oarchive_options;

    /// Document mode: `{"data":{ ...your NVPs... },"jsonser":{"format":1,"types":{...}}}`
    explicit json_oarchive(std::ostream& os, options opt = {})
        : own_writer_(std::make_unique<writer>(os, opt.indent)),
          w_(own_writer_.get()),
          types_(&own_types_),
          document_(true),
          uncaught_(std::uncaught_exceptions()) {
        w_->begin_object();
        w_->key("data");
        w_->begin_object();
    }

    /// Fragment mode (used by segments and to_json): values go straight to `w`, no header.
    json_oarchive(writer& w, type_table& types)
        : w_(&w), types_(&types), document_(false), uncaught_(std::uncaught_exceptions()) {}

    json_oarchive(const json_oarchive&) = delete;
    json_oarchive& operator=(const json_oarchive&) = delete;

    ~json_oarchive() {
        if (!finished_ && document_ && std::uncaught_exceptions() == uncaught_) {
            try {
                finish();
            } catch (...) {
            }
        }
    }

    /// Closes the document (writes the types table) and flushes. Called by the destructor too.
    void finish() {
        if (finished_) return;
        finished_ = true;
        if (document_) {
            w_->end_object();  // data
            w_->key("jsonser");
            w_->begin_object();
            w_->key("format");
            w_->value_uint(format_version);
            w_->key("types");
            detail::write_types(*w_, types_->to_map());
            w_->end_object();
            w_->end_object();
            w_->raw("\n");
        }
        w_->flush();
    }

    template <class T>
    json_oarchive& operator<<(const T& t) {
        return *this & t;
    }

    template <class T>
    json_oarchive& operator&(const T& t) {
        if constexpr (is_nvp<T>::value) {
            w_->key(t.name);
            save_value(t.value);
        } else if constexpr (is_base_ref<T>::value) {
            save_base(t.value);
        } else {
            static_assert(detail::always_false<T>,
                          "jsonser: wrap values in JSONSER_NVP(x) or jsonser::make_nvp(\"name\", x) - every "
                          "JSON field needs a name");
        }
        return *this;
    }

    // ---- API for serializer<T> specializations -------------------------------------------
    writer& out() { return *w_; }

    template <class T>
    void save_value(const T& v) {
        serializer<remove_cvref_t<T>>::save(*this, v);
    }

    /// Writes a user class as a JSON object. `extra()` runs right after '{' to add keys ($id, $type).
    template <class T, class Extra = detail::noop>
    void save_object(const T& t, Extra&& extra = {}) {
        w_->begin_object();
        extra();
        types_->note<T>();
        invoke_serialize(*this, const_cast<T&>(t), class_traits<T>::version);
        w_->end_object();
    }

    template <class B>
    void save_base(B& b) {
        types_->note<B>();
        invoke_serialize(*this, b, class_traits<B>::version);
    }

    // ---- shared_ptr tracking (one id per object, per archive / per stream record) ----------
    /// Returns {id, true} the first time `p` is seen, {id, false} afterwards.
    std::pair<std::uint64_t, bool> track(const void* p) {
        auto [it, inserted] = ids_.emplace(p, next_id_ + 1);
        if (inserted) ++next_id_;
        return {it->second, inserted};
    }
    void reset_tracking() {
        ids_.clear();
        next_id_ = 0;
    }

    type_table& types() { return *types_; }

private:
    std::unique_ptr<writer> own_writer_;
    writer* w_;
    type_table own_types_;
    type_table* types_;
    bool document_;
    bool finished_ = false;
    int uncaught_;
    std::unordered_map<const void*, std::uint64_t> ids_;
    std::uint64_t next_id_ = 0;
};

}  // namespace jsonser
