#pragma once

#include "KeyGenerator.hpp"
#include "Operations.hpp"
#include <cstdint>
#include <random>
#include <array>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <utility>
#include <unordered_map>
#include <optional>


namespace valid_framework {

    // How ofset we will get existing key
    // 0.0 - random keys, 1.0 - only existing
    struct HitFrequency {
        double get{1.0};
        double insert{0.0};
        double erase{1.0};
    }; // HitFrequency

    struct GeneratorConfig {
        OpWeights weights{};
        std::optional<HitFrequency> hits; // only for smart generators
    };

    template<typename Key, typename Value>
    class AbstractOperationGenerator {
    public:
        virtual void reset(uint64_t seed) = 0;
        virtual Operation<Key, Value> next() = 0;
        virtual void reconfigure(const GeneratorConfig&) { }
        virtual std::vector<std::pair<std::string, std::string>> meta() const = 0;
        virtual ~AbstractOperationGenerator() = default;
    }; // class AbstractOperationGenerator

    // Very simple. Just gen OPs depends on weights
    template<typename Key, typename Value>
    class BaseOperationGenerator : public AbstractOperationGenerator<Key, Value> {
    public:
        explicit BaseOperationGenerator(GeneratorConfig cfg, 
                                        AbstractKeyGenerator<Key, Value>& key_gen,
                                        uint64_t seed = std::random_device{}())
            : gen_(seed) 
            , key_gen_(key_gen)
            , weights_(cfg.weights)
        { 
            build();
        }

        void reset(uint64_t seed) override {
            gen_.seed(seed);
            key_gen_.reset(gen_());
        }

        Operation<Key, Value> next() override {
            int op_index = op_distr_(gen_);

            switch (op_index) {
                case 0: return InsertOp<Key, Value>{ key_gen_.next_key(), key_gen_.next_value() };
                case 1: return GetOp<Key>{ key_gen_.next_key() };
                case 2: return EraseOp<Key>{ key_gen_.next_key() };
                default: {
                    std::size_t custom_index = op_index - 3;
                    const auto& profile = weights_.custom[custom_index];

                    Key key1 = key_gen_.next_key();
                    Key key2 = key_gen_.next_key();
                    Value value = key_gen_.next_value();

                    std::size_t size_val = random_size_val();
                    return CustomOp<Key, Value>{ profile.id, key1, key2, value, size_val };
                }   
            }
        }

        std::vector<std::pair<std::string, std::string>> meta() const override {
            return weights_meta(weights_);
        }

    private:

        void build() {
            constexpr double EPS = 1e-9;
            
            std::vector<double> w_arr = {
                static_cast<double>(weights_.insert),
                static_cast<double>(weights_.get),
                static_cast<double>(weights_.erase)
            };

            for(const auto& profile : weights_.custom) {
                w_arr.push_back(static_cast<double>(profile.weight));
            }

            double sum = 0.0;
            for(const double w : w_arr) {
                sum += w;
            }

            if(std::fabs(sum - 100.0) > EPS) {
                throw std::invalid_argument("Sum of operation weights must be 100");
            }

            op_distr_ = std::discrete_distribution<int>(w_arr.begin(), w_arr.end());
        }

        std::size_t random_size_val() {
            std::uniform_int_distribution<std::size_t> dice(0, std::numeric_limits<std::size_t>::max());
            return dice(gen_);
        }
        
        std::mt19937_64 gen_;
        AbstractKeyGenerator<Key,Value>& key_gen_;
        OpWeights weights_;

        std::discrete_distribution<int> op_distr_;
    }; // class BaseOperationGenerator

    template<typename Key, typename Value>
    class SmartOperationGenerator : public AbstractOperationGenerator<Key, Value> {
    public:

        explicit SmartOperationGenerator(
                GeneratorConfig cfg,
                AbstractKeyGenerator<Key, Value>& key_gen,
                uint64_t seed = std::random_device{}())
            : gen_(seed)
            , key_gen_(key_gen)
            , weights_(cfg.weights)
            , hit_distr_(0.0, 1.0)
        {
            hits_ = (cfg.hits.has_value() ? *cfg.hits : HitFrequency{});
            build();
        }

        Operation<Key, Value> next() override {
            int op_index = op_distr_(gen_);

            switch(op_index) {
                case 0: {
                    bool is_hit = should_hit(hits_.insert);
                    Key key = gen_key_smart(is_hit);
                    Value value = key_gen_.next_value();

                    // Yes, we can get duplicates sometimes (if random hit existing)
                    if(!is_hit) {
                        existing_keys_.push_back(key);
                    }
                    
                    return InsertOp<Key, Value>{ key, value };
                } 
                case 1: {
                    bool is_hit = should_hit(hits_.get);
                    Key key = gen_key_smart(is_hit);
                    return GetOp<Key>{ key };
                } 
                case 2: {
                    bool is_hit = should_hit(hits_.erase);
                    if(!is_hit) {
                        return EraseOp<Key>{ key_gen_.next_key() };
                    }
                    
                    std::size_t index = random_index(existing_keys_.size());
                    Key key = existing_keys_[index];
                    existing_keys_[index] = existing_keys_.back();
                    existing_keys_.pop_back();

                    return EraseOp<Key>{ key };
                } 
                default: {
                    std::size_t custom_index = op_index - 3;
                    const auto& profile = weights_.custom[custom_index];

                    bool is_hit1 = should_hit(profile.frequency);
                    bool is_hit2 = should_hit(profile.frequency);
                    bool is_hit3 = should_hit(profile.frequency);

                    Key key1 = gen_key_smart(is_hit1);
                    Key key2 = gen_key_smart(is_hit2);
                    Value value = key_gen_.next_value();

                    std::size_t size_val = (is_hit3 ? random_index(existing_keys_.size()) : random_size_val());
                    return CustomOp<Key, Value>{ profile.id, key1, key2, value, size_val };
                }
            }
        }

        void reset(uint64_t seed) override {
            gen_.seed(seed);
            key_gen_.reset(gen_());
            existing_keys_.clear();
        }

        void reconfigure(const GeneratorConfig& cfg) override {
            weights_ = cfg.weights;
            if(cfg.hits.has_value()) {
                hits_ = *cfg.hits;
            }

            build();
        }

        std::vector<std::pair<std::string, std::string>> meta() const override {
            auto res = weights_meta(weights_);

            res.push_back({ "hit_get",    std::to_string(hits_.get) });
            res.push_back({ "hit_insert", std::to_string(hits_.insert) });
            res.push_back({ "hit_erase",  std::to_string(hits_.erase) });

            return res;
        }

    private:

        void build() {
            constexpr double EPS = 1e-9;
            
            std::vector<double> w_arr = {
                static_cast<double>(weights_.insert),
                static_cast<double>(weights_.get),
                static_cast<double>(weights_.erase)
            };

            for(const auto& profile : weights_.custom) {
                w_arr.push_back(static_cast<double>(profile.weight));
            }

            double sum = 0.0;
            for(const double w : w_arr) {
                sum += w;
            }
            if(std::fabs(sum - 100.0) > EPS) {
                throw std::invalid_argument("Sum of operation weights must be 100");
            }

            op_distr_ = std::discrete_distribution<int>(w_arr.begin(), w_arr.end());
        }

        bool should_hit(double frequency) {
            return (existing_keys_.empty() ? false : (hit_distr_(gen_) < frequency));
        }

        std::size_t random_index(std::size_t size) {
            std::uniform_int_distribution<std::size_t> dice(0, size - 1);
            return dice(gen_);
        }

        std::size_t random_size_val() {
            std::uniform_int_distribution<std::size_t> dice(0, std::numeric_limits<std::size_t>::max());
            return dice(gen_);
        }

        Key pick_existing() {
            return existing_keys_.empty() ? key_gen_.next_key() : existing_keys_[random_index(existing_keys_.size())];
        }

        Key gen_key_smart(bool is_hit) {
            return (is_hit ? pick_existing() : key_gen_.next_key());
        }

        std::mt19937_64 gen_;
        AbstractKeyGenerator<Key,Value>& key_gen_;
        
        OpWeights weights_;
        HitFrequency hits_;
        
        std::vector<Key> existing_keys_;

        std::discrete_distribution<int> op_distr_;
        std::uniform_real_distribution<double> hit_distr_;
    }; // SmartOperationGenerator

} // namespace valid_framework
