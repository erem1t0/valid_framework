#pragma once

#include <set>
#include <optional>

#include <valid_framework/ContainerWrapper.hpp>

// An example-wrapper for std::set
template<typename Key, typename Value>
struct SetWrapper : public valid_framework::DefaultWrapper<std::set<Key>, Key, Value> {

    static std::optional<Value> get(std::set<Key>& container, Key key) {
         return (container.contains(key) ? std::optional<Value>(1) : std::nullopt);
    }

    static std::optional<bool> insert(std::set<Key>& container, Key key, Value /*value*/) {
        auto res = container.insert(key);
        return std::optional<bool>(res.second);
    }

    static std::optional<bool> erase(std::set<Key>& container, Key key) {
        return std::optional<bool>(container.erase(key) > 0);
    }

    // No custom and get_memory
};
