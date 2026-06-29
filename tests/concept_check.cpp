#include <optional>
#include <set>
#include <stdexcept>
#include <cstddef>

#include <valid_framework/ContainerWrapper.hpp>
#include <valid_framework/Operations.hpp>


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

    static valid_framework::OperationResult<Key, Value> custom(std::set<Key>& container, uint32_t id, Key key1, Key key2, 
                                              Value value, std::size_t size_val) {
        // First >=
        if(id == 1) {
            auto it = container.lower_bound(key1);
            return (it == container.end() ? valid_framework::KeyResult<Key>{std::nullopt} : valid_framework::KeyResult<Key>{*it});
        } 
        // Last <=
        else if(id == 2) {
            auto it = container.upper_bound(key1);
            if(it == container.begin()) {
                return valid_framework::KeyResult<Key>{std::nullopt};
            }

            --it;
            return valid_framework::KeyResult<Key>{*it};
        } else {
            throw std::invalid_argument("Unknown custom operation id");
        }
    }

    // Rude implementation of get_memory
    static std::size_t get_memory(std::set<Key>& container) {
        return sizeof(container) + (sizeof(Key) * container.size());
    }
    
};


int main() {
    return 0;
}

