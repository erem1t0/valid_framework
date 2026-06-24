/**
 * @file ContainerWrapper.hpp
 * @brief Concept and default wrapper for user-provided containers.
 */

#pragma once

#include <concepts>
#include <optional>
#include <cstdint>

#include "Operations.hpp"

namespace valid_framework {
    
    /**
     * @brief Concept for wrappers that connect user-provided containers to the framework.
     * 
     * The framework does not call container methods directly. Instead, it calls 
     * static methods of @p Wrapper to execute generic framework operations on
     * @p Container.
     * 
     * A wrapper adapts a container-specific API to the framework operation model.
     * It does not own the container and is expected to be stateless.
     * 
     * Requires methods:
     * 
     * - @c get returns the value associated with a key. 
     * 
     * - @c insert applies an insert operation and returns true / false / @c std::nullopt (success / already exists / failure)
     * 
     * - @c erase applies an erase operation and returns true / false / @c std::nullopt (success / already missing / failure)
     * 
     * - @c custom executes a user-defined operation.
     * 
     * - @c get_memory returns current memory usage in bytes.
     * 
     * @tparam Wrapper Adapter type satisfying the @c ContainerWrapper concept.
     * @tparam Container Container type adapted by @c Wrapper.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @note The framework treats returned @c std::optional values as operation results.
     * Wrapper implementations must define what std::nullopt means: missing value,
     * caught exception or another container-specific failure policy.
     * 
     * @warning Wrappers used for validation should implement the same operation
     * semantics as wrappers used for testing. For example, both wrappers should
     * agree whether @c insert means insert-only or insert-or-assign.
     * 
     * @see Operation
     * @see OperationResult
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

    /**
     * @brief Plug wrapper with no-op implementations.
     * 
     * Custom wrappers may inherit from DefaultWrapper to avoid implementing all methods.
     * Operation methods return empty results and @c get_memory returns zero.
     *
     * This is useful when a container supports only a subset of framework
     * operations and the remaining methods should have a default behavior.
     * 
     * @tparam Container Container type adapted by the wrapper.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @warning DefaultWrapper does not perform meaningful container operations
     * by itself. It is intended as a base or placeholder for custom wrappers.
     */
    template<typename Container, typename Key, typename Value>
    struct DefaultWrapper {
        /**
         * @brief Default get implementation.
         * 
         * @param[in,out] container Ignored container instance.
         * @param[in] key Ignored operation key.
         * 
         * @return @c std::nullopt.
         */
        static std::optional<Value> get(Container&, Key) { return std::nullopt; }

        /**
         * @brief Default insert implementation.
         * 
         * @param[in,out] container Ignored container instance.
         * @param[in] key Ignored operation key.
         * @param[in] value Ignored operation value.
         * 
         * @return @c std::nullopt.
         */
        static std::optional<bool> insert(Container&, Key, Value) { return std::nullopt; }
        
        /**
         * @brief Default erase implementation.
         * 
         * @param[in,out] container Ignored container instance.
         * @param[in] key Ignored operation key.
         * 
         * @return @c std::nullopt.
         */
        static std::optional<bool> erase(Container&, Key) { return std::nullopt; }
        
        /**
         * @brief Default custom operation implementation.
         *
         * @param[in,out] container Ignored container instance.
         * @param[in] id Ignored custom operation identifier.
         * @param[in] key1 Ignored first key argument.
         * @param[in] key2 Ignored second key argument.
         * @param[in] value Ignored value argument.
         * @param[in] size_val Ignored positional argument.
         *
         * @return Boolean operation result containing @c std::nullopt.
         */
        static OperationResult<Key, Value> custom(Container&, uint32_t, Key, Key, Value, std::size_t) { return BoolResult{std::nullopt}; }
        
       /**
         * @brief Default get_memory implementation.
         *
         * @param[in] container Ignored container instance.
         *
         * @return zero.
         */
        static std::size_t get_memory(Container&) { return 0; }
    };

} // namespace valid_framework