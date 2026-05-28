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

    template<typename Key, typename Value>
    struct ScenarioConfig {

        std::size_t ops_count{0};
        std::unique_ptr<AbstractOperationGenerator<Key, Value>> op_gen;
        OpWeights weights;

        std::vector<std::pair<std::string, std::string>> meta() const {
            std::vector<std::pair<std::string, std::string>> res = {
                { "ops_count",      std::to_string(ops_count) },
                { "w_insert",       std::to_string(weights.insert) },
                { "w_get",          std::to_string(weights.get) },
                { "w_erase",        std::to_string(weights.erase) },
            };

            for(const auto& profile : weights.custom) {
                res.push_back({ "w_custom" + std::to_string(profile.id), std::to_string(profile.weight)});
            }
            return res;
        }
    };

    
    template<typename Key, typename Value>
    using PhaseConfig = ScenarioConfig<Key, Value>;

    template<typename Key, typename Value>
    class AbstractScenario {
    public:
        virtual void reset(uint64_t seed) = 0;
        virtual bool next(Operation<Key, Value>& op) = 0; // false if no more ops
        virtual std::string to_string() const = 0; // For logger
        virtual ~AbstractScenario() = default;
    }; // ScenarioConfig

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
        ScenarioConfig<Key, Value> cfg_;
        std::size_t generated_;

    }; // class BaseScenario

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


    template<typename Key, typename Value>
    class PhasedScenario : public PhasedScenarioBase<Key, Value> {
    public:

        PhasedScenario(std::vector<PhaseConfig<Key, Value>> phases)
            : phases_(std::move(phases))
        { 
            if(phases_.empty()) {
                throw std::invalid_argument("PhasedScenario mush have at least one phase");
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

    struct StatefulPhaseConfig {
        std::size_t ops_count;
        GeneratorConfig gen_cfg;

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

    // the same, but only 1 generator for each phase
    template<typename Key, typename Value>
    class StatefulPhasedScenario : public PhasedScenarioBase<Key, Value> {
    public:

        StatefulPhasedScenario(std::unique_ptr<AbstractOperationGenerator<Key, Value>> op_gen,
                               std::vector<StatefulPhaseConfig>& phases)
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
