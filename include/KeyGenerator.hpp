#pragma once

#include <random>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace valid_framework {

    template<typename Key, typename Value>
    class AbstractKeyGenerator {
    public:
        virtual void reset(uint64_t seed) = 0;
        virtual Key next_key() = 0;
        virtual Value next_value() = 0;
        virtual ~AbstractKeyGenerator() = default;
    }; // class AbstractKeyGenerator

    template<typename Key, typename Value>
    class BaseKeyGenerator : public AbstractKeyGenerator<Key, Value> {
    public:
        
        explicit BaseKeyGenerator(uint64_t seed = std::random_device{}())
            : gen_(seed)
            , key_distr_(std::numeric_limits<Key>::lowest(), std::numeric_limits<Key>::max())
            , value_distr_(std::numeric_limits<Value>::lowest(), std::numeric_limits<Value>::max())
        {
            static_assert(std::is_integral_v<Key>, "BaseKeyGenerator expects integral Key type");
            static_assert(std::is_integral_v<Value>, "BaseKeyGenerator expects integral Value type");
        }

        void reset(uint64_t seed) override {
            gen_.seed(seed);
        }

        Key next_key() override {
            return key_distr_(gen_);
        }

        Value next_value() override {
            return value_distr_(gen_);
        }

    protected:

        std::mt19937_64 gen_;
        std::uniform_int_distribution<Key>     key_distr_;
        std::uniform_int_distribution<Value> value_distr_;
    };

    template<typename Key, typename Value>
    class SetKeyGenerator : public BaseKeyGenerator<Key, Value> {
    public:

        using BaseKeyGenerator<Key, Value>::BaseKeyGenerator;

        Value next_value() override {
            return static_cast<Value>(1);
        }

    }; // class BaseKeyGenerator

    template<typename Key, typename Value>
    class XoShiroKeyGenerator : public AbstractKeyGenerator<Key, Value> {
        public:

        explicit XoShiroKeyGenerator(uint64_t seed = std::random_device{}()) {
            if(seed == 0) {
                seed = 1; // for safety
            }

            reset(seed);
        }

        void reset(uint64_t seed) override {
            uint64_t x = seed;
            for(std::size_t i = 0; i < 4; ++i) {
                state_.s[i] = splitmix64(x);
            }

        }

        Key next_key() override {
            return static_cast<Key>(xoshiro256pp());
        }

        Value next_value() override {
            return static_cast<Value>(xoshiro256pp());
        }

        private:
        
        // ===== XOSHIRO IMPLEMENTATION =====
        static uint64_t rol64(uint64_t x, int k) {
            return (x << k) | (x >> (64 - k));
        }

        struct Xoshiro256ppState {
            uint64_t s[4];
        } state_;

        uint64_t xoshiro256pp() {
            uint64_t* s = state_.s;
            const uint64_t result = rol64(s[0] + s[3], 23) + s[0];
            const uint64_t t = s[1] << 17;

            s[2] ^= s[0];
            s[3] ^= s[1];
            s[1] ^= s[2];
            s[0] ^= s[3];

            s[2] ^= t;
            s[3] = rol64(s[3], 45);

            return result;
        }

        static uint64_t splitmix64(uint64_t& x) {
            uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            return z ^ (z >> 31);
        }
        // ================================

    }; // class XoShiroKeyGenerator

} // namespace valid_framework
