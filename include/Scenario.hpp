/**
 * @file Scenario.hpp
 * @brief Defines the scenarios interface and implementations.
 */

#pragma once

#include "OperationGenerator.hpp"
#include "Operations.hpp"
#include <cstdint>
#include <string>
#include <functional>
#include <utility>
#include <vector>
#include <memory>

namespace valid_framework {

    /**
     * @brief Configuration for a fixed-length scenario.
     * 
     * Stores the number of operations to generate and the operation generator
     * used to produce them.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     * 
     * @warning op_gen must be initialized before the configuration is used.
     */
    template<typename Key, typename Value>
    struct ScenarioConfig {

        std::size_t ops_count{0}; ///< Number of operations produced before the scenario is exhausted.
        std::unique_ptr<AbstractOperationGenerator<Key, Value>> op_gen; ///< Operation generator owned by the scenario configuration.

        /**
         * @brief Returns scenario metadata for logging. 
         * 
         * @return Metadata key-value pairs containing the operation count and
         * generator metadata.
         * 
         * @pre op_gen must not be null.
         */
         std::vector<std::pair<std::string, std::string>> meta() const {
            std::vector<std::pair<std::string, std::string>> res = {
                { "ops_count", std::to_string(ops_count) },
            };

            auto gen_meta = op_gen->meta();
            res.insert(res.begin(), gen_meta.begin(), gen_meta.end());

            return res;
        }
    };
    
    /**
     * @brief Alias for phases in PhasedScenario
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
     template<typename Key, typename Value>
    using PhaseConfig = ScenarioConfig<Key, Value>;

    /**
     * @brief Interface for deterministic operation sequences.
     * 
     * A scenario produces operations one by one and can be reset to a
     * reproducible state using a seed.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */ 
    template<typename Key, typename Value>
    class AbstractScenario {
    public:
        
        /**
         * @brief Resets the scenario to a reproducible state.
         * 
         * @param[in] seed Seed used to initialize the underlying operation generator.
         */
        virtual void reset(uint64_t seed) = 0; 

        /**
         * @brief Produces the next operation.
         * 
         * @param[out] op Receives the generated operation.
         * 
         * @return true if an operation was produced, false if the scenario is exhausted.
         */
        virtual bool next(Operation<Key, Value>& op) = 0;

        virtual std::string to_string() const = 0; ///< Metadata for logging.

        virtual ~AbstractScenario() = default;
    }; // ScenarioConfig

    /**
     * @brief Scenario that produces a fixed number of generated operations.
     * 
     * BaseScenario delegates operation generation to the configured operation
     * generator and stops after @c ScenarioConfig::ops_count operations. 
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value>
    class BaseScenario : public AbstractScenario<Key, Value> {
    public:

        explicit BaseScenario(ScenarioConfig<Key, Value> cfg)
            : cfg_(std::move(cfg))
            , generated_(0)
        { }

        void reset(uint64_t seed) override {
            generated_ = 0;
            cfg_.op_gen->reset(seed);
        }

        bool next(Operation<Key, Value>& op) override {
            if(generated_ == cfg_.ops_count) {
                return false;
            }

            op = cfg_.op_gen->next();
            ++generated_;
            return true;
        }

        std::string to_string() const override {
            std::string res = "BaseScenario(\n";
            auto meta = cfg_.meta();
            for(const auto& [name, data] : meta) {
                res += name + " = " + data + "\n";
            } 
            res += ")\n";

            return res;
        }

    protected:

        /// Configuration of scenario.
        ScenarioConfig<Key, Value> cfg_;
        /// Count of generated operations.
        std::size_t generated_;

    }; // class BaseScenario

    /**
     * @brief Base class for scenarios split into consecutive phases.
     * 
     * Each phase produces a fixed number of operations. Derived classes define
     * how phase configuration is stored, how generators are reset, and what
     * happens when switching phases.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value>
    class PhasedScenarioBase : public AbstractScenario<Key, Value> {
    public:

        void reset(uint64_t seed) override final {
            current_phase_ = 0;
            current_generated_ = 0;

            do_reset(seed);
            if(get_phase_count() > 0) {
                phase_switch(0);
            }
        }

        bool next(Operation<Key, Value>& op) override final {
            if(current_phase_ == get_phase_count()) {
                return false;
            }

            if(current_generated_ == get_ops_in_phase(current_phase_)) {
                current_generated_ = 0;
                ++current_phase_;
                if(current_phase_ == get_phase_count()) {
                    return false;
                }
                phase_switch(current_phase_);
            }

            op = generate_op();
            ++current_generated_;
            return true;
        }

    protected:

        virtual void do_reset(uint64_t seed) = 0;
        virtual std::size_t get_phase_count() const = 0;
        virtual std::size_t get_ops_in_phase(std::size_t phase) const = 0;
        virtual Operation<Key, Value> generate_op() = 0;
        virtual void phase_switch(std::size_t new_phase) = 0;

        std::size_t current_phase_{0};
        std::size_t current_generated_{0};
    }; // PhasedScenarioBase


    /**
     * @brief Basic phased scenario class.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value>
    class PhasedScenario : public PhasedScenarioBase<Key, Value> {
    public:

        /**
         * @brief Constructs a phased scenario with one generator per phase.
         *
         * @param[in] phases Phase configurations in execution order.
         *
         * @throws std::invalid_argument if @p phases is empty or any phase has
         * zero operations.
         */
        PhasedScenario(std::vector<PhaseConfig<Key, Value>> phases)
            : phases_(std::move(phases))
        { 
            if(phases_.empty()) {
                throw std::invalid_argument("PhasedScenario must have at least one phase");
            }

            for(const auto& phase : phases_) {
                if(phase.ops_count == 0) {
                    throw std::invalid_argument("Each phase must have ops_count > 0");
                }
            }
        }

        std::string to_string() const override {
            std::string res = "PhasedScenario:\n";
            std::size_t curr_phase = 1;

            for(const auto& phase : phases_) {
                res += "\tPhase " + std::to_string(curr_phase++) + "(\n"; 
                
                auto meta = phase.meta();
                for(const auto& [name, data] : meta) {
                    res += name + " = " + data + "\n";
                } 
                
                res += "\n";
            }
            
            res += ")\n";
            
            return res;
        }

    protected:
    
        void do_reset(uint64_t seed) override {
            std::mt19937_64 seed_gen(seed);

            for(auto& phase : phases_) {
               phase.op_gen->reset(seed_gen());
            }
        }

        std::size_t get_phase_count() const override { return phases_.size(); }
        std::size_t get_ops_in_phase(std::size_t phase) const override { return phases_[phase].ops_count; }
    
        Operation<Key, Value> generate_op() override {
            return phases_[this->current_phase_].op_gen->next();
        }

        void phase_switch(std::size_t phase) override { }

    private:

        std::vector<ScenarioConfig<Key, Value>> phases_;

    }; // class PhasedScenario

    /**
     * @brief Phase configuration for StatefulPhasedScenario.
     * 
     * Unlike PhasedScenario, StatefulPhasedScenario uses a single operation
     * generator for all phases and reconfigures it when switching phases.
     * 
     */
    struct StatefulPhaseConfig {
        std::size_t ops_count;      ///< Number of operations in this phase.
        GeneratorConfig gen_cfg;    ///< Generator configuration applied at phase start.

        /// @brief Return metadata about StatefulPhase configuration.  
        std::vector<std::pair<std::string, std::string>> meta() const {
            std::vector<std::pair<std::string, std::string>> res = {
                { "ops_count",      std::to_string(ops_count) },
                { "w_insert",       std::to_string(gen_cfg.weights.insert) },
                { "w_get",          std::to_string(gen_cfg.weights.get) },
                { "w_erase",        std::to_string(gen_cfg.weights.erase) },
            };

            for(const auto& profile : gen_cfg.weights.custom) {
                res.push_back({ "w_custom" + std::to_string(profile.id), std::to_string(profile.weight)});
            }
            return res;
        }
    };

    /**
     * @brief Phased scenario that reuses one stateful operation generator.
     * 
     * The same generator instance is used across all phases. On each phase switch,
     * the generator is reconfigured with the corresponding phase configuration.
     * 
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value>
    class StatefulPhasedScenario : public PhasedScenarioBase<Key, Value> {
    public:

        StatefulPhasedScenario(std::unique_ptr<AbstractOperationGenerator<Key, Value>> op_gen,
                               std::vector<StatefulPhaseConfig> phases)
            : op_gen_(std::move(op_gen))
            , phases_(phases)
        { }


        std::string to_string() const override {
                std::string res = "StatefulPhasedScenario:\n";
                std::size_t curr_phase = 1;

                for(const auto& phase : phases_) {
                    res += "\tPhase " + std::to_string(curr_phase++) + "(\n"; 
                    
                    auto meta = phase.meta();
                    for(const auto& [name, data] : meta) {
                        res += name + " = " + data + "\n";
                    } 
                    
                    res += "\n";
                }
                
                res += ")\n";
                
                return res;
        }

    protected:

        void do_reset(uint64_t seed) override {
            op_gen_->reset(seed);
        }

        std::size_t get_phase_count() const override { return phases_.size(); }
        std::size_t get_ops_in_phase(std::size_t phase) const override { return phases_[phase].ops_count; }

        Operation<Key, Value> generate_op() override {
            return op_gen_->next();
        }

        void phase_switch(std::size_t new_phase) override {
            op_gen_->reconfigure(phases_[new_phase].gen_cfg);
        }

    private:

        std::unique_ptr<AbstractOperationGenerator<Key, Value>> op_gen_;
        std::vector<StatefulPhaseConfig> phases_;

    }; // PhasedUScenario


} // namespace valid_framework
