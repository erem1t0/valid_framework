/**
 * @file Operations.hpp
 * @brief Defines operation types and their results.
 */

#pragma once

#include <cstdint>
#include <variant>
#include <optional>
#include <vector>
#include <string>
#include <utility>
#include <type_traits>

namespace valid_framework {
    
    /// @brief Generation profile for a custom operation.
    struct CustomOpProfile {
        uint32_t id;            ///< Custom operation identifier of this custom operation.
        uint32_t weight{0};     ///< Generation weight of this custom operation.
        double hit_rate{0.0};  ///< Probability of selectiong existing keys for this custom operation.
    };

    /// @brief Weights of operations for generation.
    struct OpWeights {
        uint32_t get{0};    ///< Weight of GetOp operation.
        uint32_t insert{0}; ///< Weight of InsertOp operation.
        uint32_t erase{0};  ///< Weight of EraseOp operation.
        std::vector<CustomOpProfile> custom{};  ///< Custom operation generation profiles.
    };

    /// @brief Converts operationg weight to logger metadata.
    /// @param weights Target OpWeights object.
    /// @return Metadata key-value pairs describing operation weights.
    inline std::vector<std::pair<std::string, std::string>> weights_meta(const OpWeights& weights) {
        std::vector<std::pair<std::string, std::string>> res = {
            { "w_insert", std::to_string(weights.insert) },
            { "w_get",    std::to_string(weights.get) },
            { "w_erase",  std::to_string(weights.erase) },
        };

        for (const auto& profile : weights.custom) {
            res.push_back({ "w_custom" + std::to_string(profile.id), std::to_string(profile.weight) });
        }

        return res;
    }

    /// @brief Represents an insert operation.
    /// @tparam Key Operation key type.
    /// @tparam Value Operation value type.
    template<typename Key, typename Value>
    struct InsertOp { Key key; Value value; };

    /// @brief Represents an lookup operation by key.
    /// @tparam Key Operation key type.
    template<typename Key>
    struct GetOp { Key key; };

    /// @brief Represents an erase operation by key.
    /// @tparam Key Operation key type.
    template<typename Key>
    struct EraseOp { Key key; };

    /// @brief Represents a user-defined operation.
    /// @tparam Key Operation key type.
    /// @tparam Value Operation value type.
    template<typename Key, typename Value>
    struct CustomOp {
        uint32_t id;    ///< index of custom operation.
        Key key1;       ///< first key of custom operation.
        Key key2;       ///< second key of custom operation (for range-based operations).
        Value value;    ///< value of custom operation.
        std::size_t size_val;   ///< position value of custom operation.
    };

    /// @brief Operation type alias.
    /// @tparam Key Operation key type. 
    /// @tparam Value Operation value type.
    template<typename Key, typename Value>
    using Operation = std::variant<GetOp<Key>, InsertOp<Key, Value>, EraseOp<Key>, CustomOp<Key, Value>>;

    /// @brief Operation result containing an optional key.
    /// @tparam Key Operation result return type.
    template<typename Key> 
    struct KeyResult { 
        std::optional<Key> res; ///< Returned key, or std::nullopt according to wrapper semantics.
        bool operator==(const KeyResult&) const = default;
    };

    /// @brief Operation result containing an optional value.
    /// @tparam Value Operation result return type.
    template<typename Value> 
    struct ValueResult { 
        std::optional<Value> res; ///< Returned value, or std::nullopt according to wrapper semantics.
        bool operator==(const ValueResult&) const = default;
     };

    /// @brief Operation result containing an optional boolean.
    struct BoolResult { 
        std::optional<bool> res; ///< Returned boolean, or std::nullopt according to wrapper semantics.
        bool operator==(const BoolResult&) const = default;
    };
    
    /// @brief Operation result containing an optional size.
    struct CountResult { 
        std::optional<std::size_t> res; ///< Returned size, or std::nullopt according to wrapper semantics.
        bool operator==(const CountResult&) const = default;
     };

    /// @brief Operation result type alias.
    /// @tparam Key Operation key type. 
    /// @tparam Value Operation value type.
    template<typename Key, typename Value>
    using OperationResult = std::variant<KeyResult<Key>, ValueResult<Value>, 
                                         BoolResult, CountResult>;

    /** 
     * @brief Returns the textual name of an operation variant.
     * @tparam Key Operation key type. 
     * @tparam Value Operation value type.
     * @param op Target operation object.
     * @return String literal: "insert", "get", "erase" or "custom".
     */
    template<typename Key, typename Value>
    constexpr const char* op_to_string(const Operation<Key, Value>& op) {
        return std::visit([](auto&& x) {
            using T = std::decay_t<decltype(x)>;

            if constexpr (std::is_same_v<T, InsertOp<Key, Value>>) {
                return "insert";
            } else if constexpr(std::is_same_v<T, GetOp<Key>>) {
                return "get";
            }  else if constexpr(std::is_same_v<T, EraseOp<Key>>) {
                return "erase";
            } else if constexpr(std::is_same_v<T, CustomOp<Key, Value>>) {
                return "custom";
            } else {
                //                   false
                static_assert(sizeof(T) == 0, "unknown Operation type");
            }
        }, op);
    }

} // namespace valid_framework
