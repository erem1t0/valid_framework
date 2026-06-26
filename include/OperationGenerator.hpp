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

    /**
     * @brief Hit probabilities for smart operation generation.
     * 
     * A hit means that the generator tries to use a key currently known as existing. 
     * A miss means that the generator asks the key generator for a random key.
     *
     * Values are probabilities in the range [0.0, 1.0].
     *
     * @note A miss can still accidentally produce an existing key if the key
     * generator returns a duplicate key.
     */
    struct HitRate {
        double get{1.0};    ///< Hit probability for GetOp.
        double insert{0.0}; ///< Hit probability for InsertOp.
        double erase{1.0};  ///< Hit probability for EraseOp.
    }; // HitRate

    /**
     * @brief Configuration shared by operation generators.
     * 
     * Contains operation generation weights and, optionally, hit probabilities used by smart generators.
     */
    struct GeneratorConfig {
        OpWeights weights{};            ///< Operation generation weights.
        std::optional<HitRate> hits;    ///< Hit probabilities for SmartOperationGenerator.
    };

    /**
     * @brief Operation generator interface.
     * 
     * @tparam Key Operation key type to generate.
     * @tparam Value Operation value type to generate.
     */
    template<typename Key, typename Value>
    class AbstractOperationGenerator {
    public:

        /// @brief Resets the generator to a deterministic state derived from @p seed.
        /// @param seed Seed (initial state) of generator to reset.
        virtual void reset(uint64_t seed) = 0;

        /// @brief Generates the next operation from the current generator state.
        virtual Operation<Key, Value> next() = 0;

        /**
         *  @brief Updates generator configuration.
         * 
         * The default implementation does nothing. Stateful generators may override
         * this method to support phase switching.
         *
         * @param[in] cfg New generator configuration.
         */
         virtual void reconfigure(const GeneratorConfig& cfg) { }

        /**
         * @brief Returns generator metadata for logging.
         *
         * @return Metadata key-value pairs describing the current generator configuration.
         */
        virtual std::vector<std::pair<std::string, std::string>> meta() const = 0;

        virtual ~AbstractOperationGenerator() = default;
    }; // class AbstractOperationGenerator

    /**
     * @brief Generates operations according to configured operation weights.
     * 
     * This generator does not track existing keys. Every operation argument is
     * produced directly by the associated key/value generator. 
     * 
     * @tparam Key Operation key type to generate.
     * @tparam Value Operation value type to generate.
     * 
     * @note @c GeneratorConfig::hits is ignored by this generator.
     */
    template<typename Key, typename Value>
    class BaseOperationGenerator : public AbstractOperationGenerator<Key, Value> {
    public:

        /**
         * @brief Constructs a weighted operation generator.
         *
         * @param[in] cfg Generator configuration.
         * @param[in,out] key_gen Key/value generator used to produce operation arguments.
         * @param[in] seed Initial seed for operation type selection.
         *
         * @throws std::invalid_argument if operation weights do not sum to 100.
         */
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

        /**
         * @brief Build discrete distribution before using it.
         * 
         * @throws std::invalid_argument if sum of the operation weights is not 100.
         * @todo Change operation weights from int to double
         */
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

        /// @brief Generates random positional value. 
        std::size_t random_size_val() {
            std::uniform_int_distribution<std::size_t> dice(0, std::numeric_limits<std::size_t>::max());
            return dice(gen_);
        }
        
        /// Generator
        std::mt19937_64 gen_;

        /// Keys and values generator.
        AbstractKeyGenerator<Key,Value>& key_gen_;
        
        /// Operation generation weights.
        OpWeights weights_;

        /// Discrete distribution used to select operation types according to weights.
        std::discrete_distribution<int> op_distr_;
    }; // class BaseOperationGenerator

    /**
     * @brief Generates operations using weights and hit probabilities.
     *
     * SmartOperationGenerator tracks keys inserted during generation and can
     * bias get, insert, erase, and custom operations toward existing keys.
     *
     * Operation types are selected according to configured operation weights.
     * Hit decisions are made using a uniform real distribution and the configured
     * hit probabilities.
     *
     * @note The generator tracks keys according to generated operations, not according
     * to actual container results.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
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
            hits_ = (cfg.hits.has_value() ? *cfg.hits : HitRate{});
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

                    bool is_hit1 = should_hit(profile.hit_rate);
                    bool is_hit2 = should_hit(profile.hit_rate);
                    bool is_hit3 = should_hit(profile.hit_rate);

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

        /**
         * @brief Returns operation weights and hit probabilities as metadata.
         *
         * @return Metadata key-value pairs used by loggers and trace headers.
         */
        std::vector<std::pair<std::string, std::string>> meta() const override {
            auto res = weights_meta(weights_);

            res.push_back({ "hit_get",    std::to_string(hits_.get) });
            res.push_back({ "hit_insert", std::to_string(hits_.insert) });
            res.push_back({ "hit_erase",  std::to_string(hits_.erase) });

            return res;
        }

    private:

        /**
         * @brief Build discrete distribution before using it.
         * 
         * @throws std::invalid_argument if sum of the operation weights is not 100.
         */
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

        /**
         * @brief Decides whether the next argument should use an existing key.
         * 
         * @param probability Hit probability in the range [0.0, 1.0].
         */
        bool should_hit(double frequency) {
            return (existing_keys_.empty() ? false : (hit_distr_(gen_) < frequency));
        }

        /**
         * @brief Pick random index from existing keys.
         * 
         * @param size Current size of all keys.
         */
         std::size_t random_index(std::size_t size) {
            std::uniform_int_distribution<std::size_t> dice(0, size - 1);
            return dice(gen_);
        }

        /// @brief Generates random positional value. 
        std::size_t random_size_val() {
            std::uniform_int_distribution<std::size_t> dice(0, std::numeric_limits<std::size_t>::max());
            return dice(gen_);
        }

        /// @brief Selects a key from the tracked existing-key set.
        Key pick_existing() {
            return existing_keys_.empty() ? key_gen_.next_key() : existing_keys_[random_index(existing_keys_.size())];
        }

        /**
         * @brief Practical method to pick existing/random key.
         * 
         * @param is_hit If true, select a tracked existing key; otherwise generate a random key.
         */
         Key gen_key_smart(bool is_hit) {
            return (is_hit ? pick_existing() : key_gen_.next_key());
        }

        /// Generator.
        std::mt19937_64 gen_;
        /// Key/Value generator.
        AbstractKeyGenerator<Key,Value>& key_gen_;
        
        /// Operation frequencies.
        OpWeights weights_;
        /// Operation HitFrequency-es.
        HitRate hits_;
        
        /// Keys tracked as existing by the generator model.
        std::vector<Key> existing_keys_;

        /// Discrete distribution to pick operations due to operation frequencies.
        std::discrete_distribution<int> op_distr_;

        /// Uniform distribution to pick keys due to hit frequencies.
        std::uniform_real_distribution<double> hit_distr_;
    }; // SmartOperationGenerator

} // namespace valid_framework
