#pragma once

#include <cstdint>
#include <variant>
#include <string>
#include <vector>
#include <type_traits>
#include <stdexcept>
#include <functional>
#include <utility>
#include <fstream>
#include <filesystem>

#include "Scenario.hpp"
#include "Operations.hpp"
#include "ContainerWrapper.hpp"
#include "Logger.hpp"
#include "OperationApplier.hpp"
#include "Trace.hpp"

namespace valid_framework {

    enum class RunnerMode {
        Validate,
        Benchmark, 
        Combined,
    };

    inline const char* mode_to_string(RunnerMode mode) {
        switch(mode) {
            case RunnerMode::Validate:      return "Validate";
            case RunnerMode::Benchmark:     return "Benchmark";
            case RunnerMode::Combined:      return "Combined";
            default:                        return "Unknown";
        }
    }

    template<typename Key, typename Value>
    struct RunnerConfig {
        uint64_t seed{};
        bool stop_on_error{true};
        RunnerMode mode{RunnerMode::Validate};
        std::size_t chunk_size{1000000};
        std::size_t chunk_count{1024}; // for reserve if know

        std::string valid_name{"valid"};
        std::string test_name{"test"};

        TraceDumpConfig trace_cfg;
    };

    template<typename Key, typename Value>
    struct RunnerReport {
        std::size_t completed_ops{0};
        std::size_t failed_ops{0};
        std::string message{};
    };

    struct Hash128 {
        uint64_t h1;
        uint64_t h2;

        bool operator!=(const Hash128& other) const {
            return (h1 != other.h1) || (h2 != other.h2);
        }
    };

    // pair of (FNV-1a, Poly) hashing
    class Hasher {
    public:

        Hasher() {
            reset();
        }

        Hash128 get_hash()  const {
            return Hash128{ state1_, state2_ };
        }

        template<typename Key, typename Value>
        void update(const OperationResult<Key, Value>& op_res) {

            std::visit([this](auto&& x) {
                uint8_t is_nullopt = x.res.has_value() ? 0 : 1;
                this->update_hash(is_nullopt);

                if(!is_nullopt) {
                    this->update_hash(*x.res);
                }
            }, op_res);
        }

        void reset() {
            state1_ = 0;
            state2_ = FNV_offset_basis_;
        }
        
    private:
        
        template<typename T>
        void update_hash(const T& val) {
            // std::string, std::vector, std::array, std::span
            if constexpr(requires { val.data(), val.size(); }) {
                using value_type = std::remove_reference_t<decltype(val)>::value_type;
                update_hash(val.data(), val.size() * sizeof(value_type));
            } 
            // base arrays 
            else if constexpr(std::is_array_v<T>) {
                update_hash(val, sizeof(val));
            } else if constexpr(std::is_trivially_copyable_v<T>) {
                update_hash(&val, sizeof(T));
            } else {
                static_assert(sizeof(T) == 0, "Unhashable type");
            }
        }

        void update_hash(const void* data, std::size_t size) {
            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
            for(std::size_t i = 0; i < size; ++i) {
                state1_ = (state1_ ^ ptr[i]) * FNV_prime_;
                state2_ = poly_add(poly_mul(state2_, base_), ptr[i]);
            }
        }

        uint64_t poly_mul(uint64_t a, uint64_t b)
        {
            uint64_t l1 = (uint32_t)a, h1 = a >> 32, l2 = (uint32_t)b, h2 = b >> 32;
            uint64_t l = l1 * l2, m = l1 * h2 + l2 * h1, h = h1 * h2;
            uint64_t ret = (l & mod_) + (l >> 61) + (h << 3) + (m >> 29) + (m << 35 >> 3) + 1;
            ret = (ret & mod_) + (ret >> 61);
            ret = (ret & mod_) + (ret >> 61);
            return ret - 1;
        }

        uint64_t poly_add(uint64_t a, uint64_t b) {
            return (a += b) < mod_ ? a : a - mod_;
        }

        static constexpr uint64_t FNV_prime_ = 1099511628211ULL;
        static constexpr uint64_t FNV_offset_basis_ = 14695981039346656037ULL;
        static constexpr uint64_t mod_ = (1ULL << 61) - 1;
        static constexpr uint64_t base_ = 1000000007ULL;

        uint64_t state1_;
        uint64_t state2_;
    
    };

    template<typename TestWrapper,  typename TestContainer, 
             typename ValidWrapper, typename ValidContainer, 
             typename Key, typename Value>
        requires ContainerWrapper<TestWrapper, TestContainer, Key, Value> && 
                 ContainerWrapper<ValidWrapper, ValidContainer, Key, Value>
    class Runner {
    public:

        Runner(AbstractScenario<Key, Value>& scenario, 
            const std::string& test_logger_filename,
            const std::string& valid_logger_filename,
            const RunnerConfig<Key, Value>& cfg,
            std::function<TestContainer()> test_build   = [](){ return TestContainer{}; },
            std::function<ValidContainer()> valid_build = [](){ return ValidContainer{}; })
            : scenario_(scenario)
            , test_logger_(test_logger_filename)
            , valid_logger_(valid_logger_filename)
            , test_build_(std::move(test_build))
            , valid_build_(std::move(valid_build))
            , cfg_(cfg)
        {
            if(cfg_.chunk_size == 0) {
                throw std::invalid_argument("chunk_size must be positive");
            } 

            if(cfg_.trace_cfg.enabled && !cfg_.stop_on_error) {
                throw std::invalid_argument("trace requires stop_on_error = true");
            }

            if(cfg_.trace_cfg.enabled && !(cfg_.trace_cfg.dump_full || cfg_.trace_cfg.dump_ops)) {
                throw std::invalid_argument(
                    "trace enabled but politics are off. Turn at least 1 of them (ops, full) or disable trace");
            }
        }

        RunnerReport<Key, Value> run() {
            
            start_meta(test_logger_, "test", cfg_.test_name.c_str());
            start_meta(valid_logger_, "valid", cfg_.valid_name.c_str());

            switch(cfg_.mode) {
                case RunnerMode::Validate: {
                    return run_validate();
                }
                case RunnerMode::Benchmark: {
                    return run_benchmark();
                }
                case RunnerMode::Combined: {
                    return run_combined();
                }
                default: {
                    throw std::runtime_error("Unknown RunnerMode");
                }
            }
        }

    private:

        void start_meta(Logger& logger, const char* type, const char* container_name) {
            logger.add_meta("type", type);
            logger.add_meta("container_name", container_name);
            logger.add_meta("seed", std::to_string(cfg_.seed));
            logger.add_meta("mode", mode_to_string(cfg_.mode));
            logger.add_meta("scenario", scenario_.to_string());
        }

        // Only for finding errors, no timings
        RunnerReport<Key, Value> run_validate() {
            RunnerReport<Key, Value> report{};

            TestContainer test_cont = test_build_();
            ValidContainer valid_cont = valid_build_();

            scenario_.reset(cfg_.seed);
            valid_logger_.start(cfg_.chunk_count);
            test_logger_.start(cfg_.chunk_count);

            std::ofstream ops_fout;
            std::ofstream full_fout;

            const bool trace_enabled = cfg_.trace_cfg.enabled;
            const bool trace_ops_enabled = trace_enabled && cfg_.trace_cfg.dump_ops;
            const bool trace_full_enabled = trace_enabled && cfg_.trace_cfg.dump_full;

            std::string ops_tmp_filename;
            std::string full_tmp_filename;

            if(trace_ops_enabled) {
                ops_tmp_filename = make_tmp_trace_filename("ops");
                ops_fout.open(ops_tmp_filename);
                
                if(!ops_fout.is_open()) {
                    throw std::runtime_error("Cannot open ops trace file: " + ops_tmp_filename);
                }

                write_header<Key, Value>(ops_fout, cfg_.seed, 0, scenario_);
            }

            if(trace_full_enabled) {
                full_tmp_filename = make_tmp_trace_filename("ops");
                full_fout.open(full_tmp_filename);
                
                if(!full_fout.is_open()) {
                    throw std::runtime_error("Cannot open ops trace file: " + full_tmp_filename);
                }

                write_header<Key, Value>(full_fout, cfg_.seed, 0, scenario_);
            }


            Operation<Key, Value> op;
            while(scenario_.next(op)) {
                auto test_cont_res = apply_op<TestWrapper, TestContainer, Key, Value>(test_cont, op);
                auto valid_cont_res = apply_op<ValidWrapper, ValidContainer, Key, Value>(valid_cont, op);
                
                ++report.completed_ops;
                test_logger_.add_count(1);
                valid_logger_.add_count(1);

                TraceOp<Key, Value> trace_op = to_trace_op<Key, Value>(report.completed_ops, op);

                if(trace_ops_enabled) {
                    write_trace_op<Key, Value>(ops_fout, trace_op);
                }

                if(trace_full_enabled) {
                    TraceRecord<Key, Value> record;
                    record.op = trace_op;
                    record.expected = valid_cont_res;

                    write_trace_record<Key, Value>(full_fout, record);
                }

                if(test_cont_res != valid_cont_res) {
                    ++report.failed_ops;
                    
                    std::string msg = "Mismatch at op: "
                        + std::to_string(report.completed_ops)
                        + ", type: " + op_to_string(op);

                    if(trace_enabled) {
                        if(ops_fout.is_open()) {
                            ops_fout.flush();
                            ops_fout.close();
                        }

                        if(full_fout.is_open()) {
                            full_fout.flush();
                            full_fout.close();
                        }

                        if(trace_ops_enabled) {
                            const std::string ops_filename = make_trace_filename(report.completed_ops, "ops");
                            std::filesystem::rename(ops_tmp_filename, ops_filename);
                            msg += ", ops_trace: " + ops_filename;
                        }

                        if(trace_full_enabled) {
                            const std::string full_filename = make_trace_filename(report.completed_ops, "full");
                            std::filesystem::rename(full_tmp_filename, full_filename);
                            msg += ", full_trace: " + full_filename;
                        }
                    }

                    test_logger_.add_error(report.completed_ops, msg);

                    if(cfg_.stop_on_error) {
                        report.message = msg;
                        break;
                    }
                }
            }

            if(ops_fout.is_open()) {
                ops_fout.close();
            }

            if(full_fout.is_open()) {
                full_fout.close();
            }

            if(trace_enabled && report.failed_ops == 0) {
                if(trace_ops_enabled && !ops_tmp_filename.empty()) {
                    std::filesystem::remove(ops_tmp_filename);
                }

                if(trace_full_enabled && !full_tmp_filename.empty()) {
                    std::filesystem::remove(full_tmp_filename);
                }
            }

            finish_and_write();

            return report;
        }

        // Only timings
        RunnerReport<Key, Value> run_benchmark() {
            RunnerReport<Key, Value> report{};
            Operation<Key, Value> op;
            std::vector<Operation<Key, Value>> ops;
            ops.reserve(cfg_.chunk_size);

            // ========== TEST ==========
            {
                TestContainer test_cont = test_build_();
                scenario_.reset(cfg_.seed);
                test_logger_.start(cfg_.chunk_count);

                while(true) {
                    ops.clear();
                    
                    for(std::size_t i = 0; i < cfg_.chunk_size; ++i) {
                        if(!scenario_.next(op)) {
                            break;
                        }
                        ops.push_back(op);
                    }

                    if(ops.empty()) {
                        break;
                    }

                    test_logger_.resume();
                    for(const auto& curr_op : ops) {
                        apply_op<TestWrapper, TestContainer, Key, Value>(test_cont, curr_op);
                    }
                    test_logger_.add_count(ops.size());
                    test_logger_.pause(TestWrapper::get_memory(test_cont));
                    report.completed_ops += ops.size();
                }
                test_logger_.finish();
            }
            // ========== VALID ========== 
            {
                ValidContainer valid_cont = valid_build_();
                scenario_.reset(cfg_.seed);
                valid_logger_.start(cfg_.chunk_count);

                while(true) {
                    ops.clear();

                    for(std::size_t i = 0; i < cfg_.chunk_size; ++i) {
                        if(!scenario_.next(op)) {
                            break;
                        }
                        ops.push_back(op);
                    }
                    if(ops.empty()) {
                        break;
                    }

                    valid_logger_.resume();
                    for(const auto& curr_op : ops) {
                        apply_op<ValidWrapper, ValidContainer, Key, Value>(valid_cont, curr_op);
                    }
                    valid_logger_.add_count(ops.size());
                    valid_logger_.pause(ValidWrapper::get_memory(valid_cont));
                }
                valid_logger_.finish();
            }

            test_logger_.write();
            valid_logger_.write();
            return report;
        }

        // Timings + error (but dont know where it is)
        RunnerReport<Key, Value> run_combined() {
            RunnerReport<Key, Value> report{};
            Operation<Key, Value> op;
            Hasher test_hasher, valid_hasher;

            std::vector<Operation<Key, Value>> ops;
            std::vector<OperationResult<Key, Value>> results;
            ops.reserve(cfg_.chunk_size);
            results.reserve(cfg_.chunk_size);

            // ========== TEST ==========
            {
                TestContainer test_cont = test_build_();
                scenario_.reset(cfg_.seed);
                test_logger_.start(cfg_.chunk_count);

                while(true) {
                    ops.clear();
                    results.clear();
                    
                    for(std::size_t i = 0; i < cfg_.chunk_size; ++i) {
                        if(!scenario_.next(op)) {
                            break;
                        }
                        ops.push_back(op);
                    }

                    if(ops.empty()) {
                        break;
                    }

                    test_logger_.resume();
                    for(const auto& curr_op : ops) {
                        results.push_back(apply_op<TestWrapper, TestContainer, Key, Value>(test_cont, curr_op));
                    }
                    test_logger_.add_count(ops.size());
                    test_logger_.pause(TestWrapper::get_memory(test_cont));
                    report.completed_ops += ops.size();
                    
                    for(const auto& res : results) {
                        test_hasher.update(res);
                    }
                }
                test_logger_.finish();
            }

            // ========== VALID ========== 
            {
                ValidContainer valid_cont = valid_build_();
                scenario_.reset(cfg_.seed);
                valid_logger_.start(cfg_.chunk_count);

                while(true) {
                    ops.clear();
                    results.clear();
                    
                    for(std::size_t i = 0; i < cfg_.chunk_size; ++i) {
                        if(!scenario_.next(op)) {
                            break;
                        }
                        ops.push_back(op);
                    }
                    if(ops.empty()) {
                        break;
                    }

                    valid_logger_.resume();
                    for(const auto& curr_op : ops) {
                        results.push_back(apply_op<ValidWrapper, ValidContainer, Key, Value>(valid_cont, curr_op));
                    }
                    valid_logger_.add_count(ops.size());
                    valid_logger_.pause(ValidWrapper::get_memory(valid_cont));

                    for(const auto& res : results) {
                        valid_hasher.update(res);
                    }
                }
                valid_logger_.finish();
            }

            if(test_hasher.get_hash() != valid_hasher.get_hash()) {
                report.failed_ops = 1; // we dont know real count
                report.message = "Total hash mismatch";
                test_logger_.add_error(report.completed_ops, report.message);
            } 

            test_logger_.write();
            valid_logger_.write();

            return report;
        }

        void finish_and_write() {
            test_logger_.finish();
            valid_logger_.finish();
            test_logger_.write();
            valid_logger_.write();
        }

        std::string make_trace_filename(std::size_t op_index, const char* type) const {
            return cfg_.trace_cfg.filename_prefix + "_seed_" + std::to_string(cfg_.seed) 
                    + "_op_" + std::to_string(op_index) + "_" + type + ".log";
        }

        std::string make_tmp_trace_filename(const char* type) const {
            return cfg_.trace_cfg.filename_prefix + "_seed_" + std::to_string(cfg_.seed) 
                     + "_" + type + "_tmp.log";
        }

        AbstractScenario<Key, Value>& scenario_;
        RunnerConfig<Key, Value> cfg_;
        Logger test_logger_;
        Logger valid_logger_;
        std::function<TestContainer()>  test_build_;
        std::function<ValidContainer()> valid_build_;

    }; // Runner

    enum  class SingleRunnerMode {
        Plain,
        Benchmark,
        Benchmark_hashed,
    }; // SingleRunnerMode
        
    inline const char* mode_to_string(SingleRunnerMode mode) {
        switch(mode) {
            case SingleRunnerMode::Plain:               return "Plain";
            case SingleRunnerMode::Benchmark:           return "Benchmark";
            case SingleRunnerMode::Benchmark_hashed:    return "Benchmark_hashed";
            default:                                    return "Unknown";
        }
    }

    template<typename Key, typename Value>
    struct SingleRunnerConfig {
        uint64_t seed{};
        SingleRunnerMode mode{SingleRunnerMode::Plain};
        std::size_t chunk_size{1000000};
        std::size_t chunk_count{1024}; // for reserve if know
        
        std::string name{"container"};
    }; // SingleRunnerConfig

    template<typename Wrapper, typename Container, typename Key, typename Value>
    requires ContainerWrapper<Wrapper, Container, Key, Value>
    class SingleRunner {
    public:

        SingleRunner(const SingleRunnerConfig<Key, Value>& cfg,
               AbstractScenario<Key, Value>& scenario, 
               const std::string& logger_filename,
               std::function<Container()> build = [](){ return Container{}; })
            : cfg_(cfg)
            , scenario_(scenario)
            , logger_(logger_filename)
            , build_(std::move(build))
        {
            if(cfg_.chunk_size == 0) {
                throw std::invalid_argument("chunk_size must be positive");
            } 
        }

        RunnerReport<Key, Value> run() { 
            start_meta("container", cfg_.name.c_str());

            switch(cfg_.mode) {
                case SingleRunnerMode::Plain: {
                    return run_plain();
                }
                case SingleRunnerMode::Benchmark: {
                    return run_benchmark();
                }
                case SingleRunnerMode::Benchmark_hashed: {
                    return run_benchmark_hashed();
                }
                default: {
                    throw std::runtime_error("Unknown SingleRunnerMode");
                }
            }
        }

    private:

        RunnerReport<Key, Value> run_plain() {
            RunnerReport<Key, Value> report{};
            Operation<Key, Value> op;

            Container cont = build_();
            scenario_.reset(cfg_.seed);
            logger_.start(cfg_.chunk_count);

            while(scenario_.next(op)) {
                apply_op<Wrapper, Container, Key, Value>(cont, op);
                ++report.completed_ops;
                logger_.add_count(1);
            }

            logger_.finish();
            logger_.write();
            return report;
        }

        RunnerReport<Key, Value> run_benchmark() {
            RunnerReport<Key, Value> report{};
            Operation<Key, Value> op;
            std::vector<Operation<Key, Value>> ops;
            ops.reserve(cfg_.chunk_size);

            Container cont = build_();
            scenario_.reset(cfg_.seed);
            logger_.start(cfg_.chunk_count);

            while(true) {
                ops.clear();
                
                for(std::size_t i = 0; i < cfg_.chunk_size; ++i) {
                    if(!scenario_.next(op)) {
                        break;
                    }
                    ops.push_back(op);
                }

                if(ops.empty()) {
                    break;
                }

                logger_.resume();
                for(const auto& curr_op : ops) {
                    apply_op<Wrapper, Container, Key, Value>(cont, curr_op);
                }
                logger_.add_count(ops.size());
                logger_.pause(Wrapper::get_memory(cont));
                report.completed_ops += ops.size();
            }

            logger_.finish();
            logger_.write();
            return report;
        }

        RunnerReport<Key, Value> run_benchmark_hashed() {
            RunnerReport<Key, Value> report{};
            Operation<Key, Value> op;
            std::vector<Operation<Key, Value>> ops;
            std::vector<OperationResult<Key, Value>> results;
            ops.reserve(cfg_.chunk_size);
            results.reserve(cfg_.chunk_size);
        
            Hasher hasher;
        
            Container cont = build_();
            scenario_.reset(cfg_.seed);
            logger_.start(cfg_.chunk_count);

            while(true) {
                ops.clear();
                results.clear();
                
                for(std::size_t i = 0; i < cfg_.chunk_size; ++i) {
                    if(!scenario_.next(op)) {
                        break;
                    }
                    ops.push_back(op);
                }

                if(ops.empty()) {
                    break;
                }

                logger_.resume();
                for(const auto& curr_op : ops) {
                    results.push_back(apply_op<Wrapper, Container, Key, Value>(cont, curr_op));
                }
                logger_.add_count(ops.size());
                logger_.pause(Wrapper::get_memory(cont));
                report.completed_ops += ops.size();

                for(const auto& res : results) {
                    hasher.update(res);
                }
            }
            
            Hash128 hash_result = hasher.get_hash();
            logger_.add_meta("hash", std::to_string(hash_result.h1) + std::to_string(hash_result.h2));

            logger_.finish();
            logger_.write();
            return report;
        }

        void start_meta(const char* type, const char* container_name) {
            logger_.add_meta("type", type);
            logger_.add_meta("container_name", container_name);
            logger_.add_meta("seed", std::to_string(cfg_.seed));
            logger_.add_meta("mode", mode_to_string(cfg_.mode));
            logger_.add_meta("scenario", scenario_.to_string());
        }

        SingleRunnerConfig<Key, Value> cfg_;
        AbstractScenario<Key, Value>& scenario_;
        Logger logger_;
        std::function<Container()> build_;
    }; // class SingleRunner

} // namespace valid_framework
