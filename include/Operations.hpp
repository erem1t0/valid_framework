#pragma once

#include <cstdint>
#include <variant>
#include <optional>
#include <vector>

namespace valid_framework {
    
    struct CustomOpProfile {
        uint32_t id;
        uint32_t weight{0};
        double frequency{0.0};
    };

    struct OpWeights {
        uint32_t get{0};
        uint32_t insert{0};
        uint32_t erase{0};
        std::vector<CustomOpProfile> custom{};
    };

    template<typename Key, typename Value>
    struct InsertOp { Key key; Value value; };

    template<typename Key>
    struct GetOp { Key key; };

    template<typename Key>
    struct EraseOp { Key key; };

    template<typename Key, typename Value>
    struct CustomOp {
        uint32_t id; 
        Key key1;
        Key key2;
        Value value;
        std::size_t pos;
    };

    template<typename Key, typename Value>
    using Operation = std::variant<GetOp<Key>, InsertOp<Key, Value>, EraseOp<Key>, CustomOp<Key, Value>>;

    template<typename Key> 
    struct KeyResult { 
        std::optional<Key> res;
        bool operator==(const KeyResult&) const = default;
    };

    template<typename Value> 
    struct ValueResult { 
        std::optional<Value> res;
        bool operator==(const ValueResult&) const = default;
     };

    struct BoolResult { 
        std::optional<bool> res;
        bool operator==(const BoolResult&) const = default;
    };
    
    struct CountResult { 
        std::optional<std::size_t> res;
        bool operator==(const CountResult&) const = default;
     };

    template<typename Key, typename Value>
    using OperationResult = std::variant<KeyResult<Key>, ValueResult<Value>, 
                                         BoolResult, CountResult>;

    template<typename Key, typename Value>
    const char* op_to_string(const Operation<Key, Value>& op) {
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
