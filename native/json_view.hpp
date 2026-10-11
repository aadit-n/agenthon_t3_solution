#pragma once
#include <simdjson.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>

namespace t3 {
using i64 = long long;

class JsonView {
    simdjson::dom::element elem;
    bool valid = true;
public:
    JsonView() : valid(false) {}
    JsonView(simdjson::dom::element e) : elem(e), valid(true) {}

    bool is_valid() const { return valid; }
    bool is_null() const { return !valid || elem.is_null(); }
    bool empty() const {
        if (!valid) return true;
        if (elem.type() == simdjson::dom::element_type::OBJECT) return elem.get_object().size() == 0;
        if (elem.type() == simdjson::dom::element_type::ARRAY) return elem.get_array().size() == 0;
        return false;
    }

    bool contains(std::string_view key) const {
        if (!valid || elem.type() != simdjson::dom::element_type::OBJECT) return false;
        auto res = elem[key];
        return res.error() == simdjson::SUCCESS;
    }

    JsonView operator[](std::string_view key) const {
        if (!valid || elem.type() != simdjson::dom::element_type::OBJECT) return JsonView();
        auto res = elem[key];
        if (res.error() != simdjson::SUCCESS) return JsonView();
        return JsonView(res.value());
    }

    JsonView at(std::string_view key) const {
        if (!valid || elem.type() != simdjson::dom::element_type::OBJECT) throw std::out_of_range("key not found: " + std::string(key));
        auto res = elem[key];
        if (res.error() != simdjson::SUCCESS) throw std::out_of_range("key not found: " + std::string(key));
        return JsonView(res.value());
    }

    template<class T> T get() const {
        if (!valid) throw std::runtime_error("invalid element");
        if constexpr(std::is_same_v<T, bool>) {
            return elem.get_bool().value();
        } else if constexpr(std::is_same_v<T, std::string>) {
            return std::string(elem.get_string().value());
        } else if constexpr(std::is_integral_v<T> && std::is_signed_v<T>) {
            if (elem.is_int64()) return T(elem.get_int64().value());
            if (elem.is_uint64()) return T(elem.get_uint64().value());
            return T(elem.get_int64().value());
        } else if constexpr(std::is_integral_v<T> && !std::is_signed_v<T>) {
            if (elem.is_uint64()) return T(elem.get_uint64().value());
            if (elem.is_int64()) return T(elem.get_int64().value());
            return T(elem.get_uint64().value());
        } else if constexpr(std::is_floating_point_v<T>) {
            if (elem.is_int64()) return T(elem.get_int64().value());
            if (elem.is_uint64()) return T(elem.get_uint64().value());
            return T(elem.get_double().value());
        } else {
            static_assert(!sizeof(T), "unsupported get type");
        }
    }

    template<class T> T value(std::string_view key, T fallback) const {
        if (!valid || elem.type() != simdjson::dom::element_type::OBJECT) return fallback;
        auto res = elem[key];
        if (res.error() != simdjson::SUCCESS) return fallback;
        simdjson::dom::element val = res.value();
        if (val.is_null()) return fallback;
        if constexpr(std::is_same_v<T, bool>) {
            auto b = val.get_bool();
            return b.error() == simdjson::SUCCESS ? b.value() : fallback;
        } else if constexpr(std::is_same_v<T, std::string>) {
            auto s = val.get_string();
            return s.error() == simdjson::SUCCESS ? std::string(s.value()) : fallback;
        } else if constexpr(std::is_same_v<T, const char*>) {
            auto s = val.get_string();
            return s.error() == simdjson::SUCCESS ? s.value().data() : fallback;
        } else if constexpr(std::is_integral_v<T> && std::is_signed_v<T>) {
            if (val.is_int64()) return T(val.get_int64().value());
            if (val.is_uint64()) return T(val.get_uint64().value());
            return fallback;
        } else if constexpr(std::is_integral_v<T> && !std::is_signed_v<T>) {
            if (val.is_uint64()) return T(val.get_uint64().value());
            if (val.is_int64()) return T(val.get_int64().value());
            return fallback;
        } else if constexpr(std::is_floating_point_v<T>) {
            if (val.is_int64()) return T(val.get_int64().value());
            if (val.is_uint64()) return T(val.get_uint64().value());
            auto d = val.get_double();
            return d.error() == simdjson::SUCCESS ? T(d.value()) : fallback;
        } else if constexpr(std::is_same_v<T, JsonView>) {
            return JsonView(val);
        } else {
            return fallback;
        }
    }

    // Iterator support for array iteration
    struct Iterator {
        simdjson::dom::array::iterator it;
        JsonView operator*() const { return JsonView(*it); }
        Iterator& operator++() { ++it; return *this; }
        bool operator!=(const Iterator& other) const { return it != other.it; }
    };

    Iterator begin() const {
        return Iterator{elem.get_array().value().begin()};
    }
    Iterator end() const {
        return Iterator{elem.get_array().value().end()};
    }
};

} // namespace t3
