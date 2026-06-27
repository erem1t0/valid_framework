#pragma once

#include <optional>

#include <valid_framework/ContainerWrapper.hpp>

// An example-wrapper for UnknownContainer
template<typename Key, typename Value>
struct UnknownContainerWrapper : public valid_framework::DefaultWrapper<UnknownContainer<Key, Value>, Key, Value> {

    static std::optional<Value> get(UnknownContainer<Key, Value>& container, Key key) {
        try {
            return std::optional<Value>(container.at(key));
        } catch (const std::out_of_range&) {
            return std::nullopt;
        }
    }

    static std::optional<bool> insert(UnknownContainer<Key, Value>& container, Key key, Value value) {
        return std::optional<bool>(container.insert(key, value));
    }

    static std::optional<bool> erase(UnknownContainer<Key, Value>& container, Key key) {
        return std::optional<bool>(container.erase(key));
    }

    static std::size_t get_memory(UnknownContainer<Key, Value>& container) {
        return container.memory_usage();
    }

    // No custom and get_memory
};
