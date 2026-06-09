#pragma once

#include <concepts>
#include <optional>
#include <cstdint>
#include "Operations.hpp"


namespace valid_framework {

    /*
        everywhere we have optional bcs some containers can return error_codes
        or exceptions. So if something bad happened we just get std::nullopt

        TODO: change std::optional<Key> to std::optional<std::pair<Key, Value>>?
    */
    
    template<typename Wrapper, typename Container, typename Key, typename Value>
    concept ContainerWrapper = requires(Container& cont, 
                                        Key key, 
                                        Value value, 
                                        std::size_t pos) {
        
        { Wrapper::get(cont, key) }                             -> std::same_as<std::optional<Value>>;   
        { Wrapper::insert(cont, key, value) }                   -> std::same_as<std::optional<bool>>;
        { Wrapper::erase(cont, key) }                           -> std::same_as<std::optional<bool>>;
        { Wrapper::custom(cont, 0u, key, key, value, pos) }     -> std::same_as<OperationResult<Key, Value>>;
        { Wrapper::get_memory(cont) }                           -> std::same_as<std::size_t>;
    };


    template<typename Container, typename Key, typename Value>
    struct DefaultWrapper {
        static std::optional<Value> get(Container&, Key) { return std::nullopt; }
        static std::optional<bool> insert(Container&, Key, Value) { return std::nullopt; }
        static std::optional<bool> erase(Container&, Key) { return std::nullopt; }
        static OperationResult<Key, Value> custom(Container&, uint32_t, Key, Key, Value, std::size_t) { return BoolResult{std::nullopt}; }
        static std::size_t get_memory(Container&) { return 0; }
    };

} // namespace valid_framework