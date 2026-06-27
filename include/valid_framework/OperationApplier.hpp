/**
 * @file OperationApplier.hpp
 * @brief Defines the apply_op function template for dispatching framework operations to container wrappers.
 */

#pragma once

#include <type_traits>
#include <variant>

#include "Operations.hpp"
#include "ContainerWrapper.hpp"

namespace valid_framework { 
    
    /**
     * @brief Applies a framework operation to a container through a wrapper.
     * 
     * Dispatches @p op to the corresponding static method of @p Wrapper:
     * @c Wrapper::insert, @c Wrapper::get, @c Wrapper::erase, or @c Wrapper::custom.
     * 
     * @tparam Wrapper Adapter type that satisfies ContainerWrapper.
     * @tparam Container Container type adapted by @c Wrapper.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @param[in,out] container Container instance to which the operation is applied.
     * @param[in] op Operation to be applied.
     * 
     * @return An OperationResult containing the result of the applied @p op.
     * 
     * @see ContainerWrapper
     * @see Operation
     * @see OperationResult
     */
    template<typename Wrapper, typename Container, typename Key, typename Value>
    requires ContainerWrapper<Wrapper, Container, Key, Value>
    OperationResult<Key, Value> apply_op(Container& container, const Operation<Key, Value>& op) {
        return std::visit(
            [&](const auto& x) -> OperationResult<Key, Value> {
                using T = std::decay_t<decltype(x)>;

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
