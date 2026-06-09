#pragma once

#include <type_traits>
#include <variant>

#include "Operations.hpp"
#include "ContainerWrapper.hpp"

namespace valid_framework { 
    
    template<typename Wrapper, typename Container, typename Key, typename Value>
    requires ContainerWrapper<Wrapper, Container, Key, Value>
    OperationResult<Key, Value> apply_op(Container& container, const Operation<Key, Value>& op) {
        return std::visit(
            [&](const auto& x) -> OperationResult<Key, Value> {
                using T = std:: decay_t<decltype(x)>;

                if constexpr(std::is_same_v<T, InsertOp<Key, Value>>) {
                    return BoolResult{Wrapper::insert(container,x.key, x.value)};
                } else if constexpr(std::is_same_v<T, GetOp<Key>>) {
                    return ValueResult<Value>{Wrapper::get(container, x.key)};
                } else if constexpr(std::is_same_v<T, EraseOp<Key>>) {
                    return BoolResult{Wrapper::erase(container, x.key)};
                } else if constexpr(std::is_same_v<T, CustomOp<Key, Value>>) {
                    return Wrapper::custom(container, x.id, x.key1, x.key2, x.value, x.size_val);
                } else {
                    static_assert(sizeof(T) == 0, "Unknown operation type");
                }
            }, op);
    }

} // namespace valid_framework
