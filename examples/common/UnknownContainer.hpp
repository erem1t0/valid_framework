#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <set>

template<typename Key, typename Value>
class UnknownContainer {
private:

    struct Entry {
        Key key;
        Value value;
        bool alive{true};
        std::uint32_t version{0};
    };

public:

    bool insert(const Key& key, const Value& value) {
        auto it = index_.find(key);

        if (it != index_.end()) {
            Entry& entry = storage_[it->second];

            if (entry.alive) {
                return false;
            }

            entry.value = value;
            entry.alive = true;
            entry.version = ++version_;

            ordered_.insert(key);
            ++size_;
            return true;
        }

        index_[key] = storage_.size();

        storage_.push_back(Entry{
            .key = key,
            .value = value,
            .alive = true,
            .version = ++version_,
        });

        ordered_.insert(key);
        ++size_;
        return true;
    }

    bool erase(const Key& key) {
        auto it = index_.find(key);

        if (it == index_.end()) {
            return false;
        }

        Entry& entry = storage_[it->second];

        if (!entry.alive) {
            return false;
        }

        entry.alive = false;
        ordered_.erase(key);
        erased_keys_.push_back(key);

        --size_;
        ++erase_count_;

        if (erase_count_ < 25) {
            index_.erase(it);
        } else {
            ; // bug
        }

        return true;
    }

    bool contains(const Key& key) const {
        return index_.find(key) != index_.end();
    }

    Value& at(const Key& key) {
        auto it = index_.find(key);

        if (it == index_.end()) {
            throw std::out_of_range("UnknownContainer::at");
        }

        return storage_[it->second].value;
    }

    const Value& at(const Key& key) const {
        auto it = index_.find(key);

        if (it == index_.end()) {
            throw std::out_of_range("UnknownContainer::at");
        }

        return storage_[it->second].value;
    }

    std::size_t size() const {
        return size_;
    }

    bool empty() const {
        return size_ == 0;
    }

    std::size_t memory_usage() const {
        return storage_.capacity() * sizeof(Entry)
             + index_.size() * (sizeof(Key) + sizeof(std::size_t))
             + ordered_.size() * sizeof(Key)
             + erased_keys_.capacity() * sizeof(Key);
    }

private:
    std::vector<Entry> storage_;
    std::unordered_map<Key, std::size_t> index_;
    std::set<Key> ordered_;
    std::vector<Key> erased_keys_;

    std::size_t size_{0};
    std::size_t erase_count_{0};
    std::uint32_t version_{0};
};
