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

    /// @brief Enumeration of runner modes.
    enum class RunnerMode {
        Validate,   ///< Runs operations one by one and compares results immediately.
        Benchmark,  ///< Runs operations in chunks and records timing and memory only.
        
        /**
         * @brief Runs operations in chunks, measures performance, and hashes results.
         *
         * This mode helps prevent dead-code elimination in benchmark runs and provides
         * a fast aggregate correctness check. It can detect that results differ, but
         * cannot identify the exact operation where the mismatch occurred.
         */
        Combined,  
    };

    /// @brief Converts runner mode to string (const char*).
    /// @param mode Target runner mode.
    inline const char* mode_to_string(RunnerMode mode) {
        switch(mode) {
            case RunnerMode::Validate:      return "Validate";
            case RunnerMode::Benchmark:     return "Benchmark";
            case RunnerMode::Combined:      return "Combined";
            default:                        return "Unknown";
        }
    }

    /**
     * @brief Configuration for the Runner class.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename Key, typename Value>
    struct RunnerConfig {
        uint64_t seed{}; ///< Seed used to reset the scenario for reproducible runs.
        bool stop_on_error{true}; ///< Stop validation after the first mismatch.
        RunnerMode mode{RunnerMode::Validate}; ///< Mode of the runner.
        std::size_t chunk_size{1000000}; ///< Size of chunks for Benchmark and Combined modes.
        std::size_t chunk_count{1024};   ///< Expected number of chunks used to reserve logger storage.

        std::string valid_name{"valid"}; ///< Display name of the validating container.
        std::string test_name{"test"};   ///< Display name of the testing container.

        TraceDumpConfig trace_cfg;  ///< Configuration for tracing operations into a file if necessary.
    };

    /**
     * @brief Summary returned by a runner after execution.
     *
     * @tparam Key Container key type. 
     * @tparam Value Container value type.
     */
     template<typename Key, typename Value>
    struct RunnerReport {
        std::size_t completed_ops{0}; ///< Number of operations completed by the runner.
        std::size_t failed_ops{0};    ///< Number of detected failures; in Combined mode this is 0 or 1.
        std::string message{};        ///< Message with information about error if it occurred.
    };

    /// @brief Struct for storing hashes.
    struct Hash128 {
        uint64_t h1;
        uint64_t h2;

        bool operator!=(const Hash128& other) const {
            return (h1 != other.h1) || (h2 != other.h2);
        }
    };

    /**
     * @brief Incrementally hashes operation results into a 128-bit aggregate hash.
     *
     * The hash combines FNV-1a and polynomial hashing and is used by combined
     * benchmark/validation modes.
     */
    class Hasher {
    public:

        Hasher() {
            reset();
        }

        /// @brief Returns the current aggregate hash.
        Hash128 get_hash()  const {
            return Hash128{ state1_, state2_ };
        }

        /**
         * @brief Incorporates an operation result into the aggregate hash.
         * 
         * @tparam Key Operation result key type.
         * @tparam Value Operation result value type.
         * 
         * @param op_res Target operation result for updating hash.
         */
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

        /// @brief Reset hash values to start state.
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


    /**
     * @brief Runs a scenario against test and reference containers.
     * 
     * Runner can validate operation results, benchmark both containers, or run
     * an aggregate hash check depending on RunnerConfig::mode.
     * 
     * @tparam TestWrapper Adapter type for the testing container.
     * @tparam TestContainer Testing container type.
     * @tparam ValidWrapper Adapter type for the validating container.
     * @tparam ValidContainer Validating container type.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
    template<typename TestWrapper,  typename TestContainer, 
             typename ValidWrapper, typename ValidContainer, 
             typename Key, typename Value>
        requires ContainerWrapper<TestWrapper, TestContainer, Key, Value> && 
                 ContainerWrapper<ValidWrapper, ValidContainer, Key, Value>
    class Runner {
    public:

        /**
         * @brief Constructs a runner for comparing or benchmarking two containers.
         *
         * @param[in,out] scenario Scenario that produces operations for the run.
         * @param[in] test_logger_filename Output log file for the tested container.
         * @param[in] valid_logger_filename Output log file for the reference container.
         * @param[in] cfg Runner configuration.
         * @param[in] test_build Factory function used to create the tested container.
         * @param[in] valid_build Factory function used to create the reference container.
         *
         * @throws std::invalid_argument if chunk_size is zero or trace configuration is invalid.
         */
        Runner(AbstractScenario<Key, Value>& scenario, 
               const std::string& test_logger_filename,
               const std::string& valid_logger_filename,
               const RunnerConfig<Key, Value>& cfg,
               std::function<TestContainer()> test_build   = [](){ return TestContainer{}; },
               std::function<ValidContainer()> valid_build = [](){ return ValidContainer{}; }
            )
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
                    "trace is enabled but both trace outputs are disabled; enable dump_ops or dump_full, or disable tracing");
            }
        }

        /**
         * @brief Executes the configured runner mode.
         *
         * Initializes metadata, runs the scenario according to RunnerConfig::mode,
         * writes logs, and returns the execution report.
         *
         * @return Summary of the completed run.
         *
         * @throws std::runtime_error if the configured runner mode is unknown.
         */
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

        /**
         * @brief Add general metadata about launching.
         * 
         * @param logger Target logger object.
         * @param type Key of metadata.
         * @param container_name Name of the container for logging.
         */
        void start_meta(Logger& logger, const char* type, const char* container_name) {
            logger.add_meta("type", type);
            logger.add_meta("container_name", container_name);
            logger.add_meta("seed", std::to_string(cfg_.seed));
            logger.add_meta("mode", mode_to_string(cfg_.mode));
            logger.add_meta("scenario", scenario_.to_string());
        }

        /// @brief Launch scenario with validate runner mode with no measurements.
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
                full_tmp_filename = make_tmp_trace_filename("full");
                full_fout.open(full_tmp_filename);
                
                if(!full_fout.is_open()) {
                    throw std::runtime_error("Cannot open full trace file: " + full_tmp_filename);
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
                    write_op<Key, Value>(ops_fout, report.completed_ops, op);
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

        /// @brief Launch scenario with benchmark runner mode only with measurements.
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

        /// @brief Launch scenario with combined runner mode with measurements and total hash mismatch checking.
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

        /// @brief Finalizes both loggers and writes their output files.
        void finish_and_write() {
            test_logger_.finish();
            valid_logger_.finish();
            test_logger_.write();
            valid_logger_.write();
        }

        /// @brief Method to collect name of trace file.
        /// @param op_index Index of operation where occured mismatch.
        /// @param type Type of tracing (full / ops only).
        std::string make_trace_filename(std::size_t op_index, const char* type) const {
            return cfg_.trace_cfg.filename_prefix + "_seed_" + std::to_string(cfg_.seed) 
                    + "_op_" + std::to_string(op_index) + "_" + type + ".log";
        }

        /// @brief Method to collect name of temporary trace file while running.
        /// @param type Type of tracing (full / ops only).
        std::string make_tmp_trace_filename(const char* type) const {
            return cfg_.trace_cfg.filename_prefix + "_seed_" + std::to_string(cfg_.seed) 
                     + "_" + type + "_tmp.log";
        }

        /// Main scenario for launch.
        AbstractScenario<Key, Value>& scenario_;
        /// Configuration of runner.
        RunnerConfig<Key, Value> cfg_;
        /// Logger for testing container.
        Logger test_logger_;
        /// Logger for validating container.
        Logger valid_logger_;
        /// Fabric for building testing container.
        std::function<TestContainer()>  test_build_;
        /// Fabric for building validating container.
        std::function<ValidContainer()> valid_build_;

    }; // Runner


    /// @brief Enumeration of single runner modes.
    enum  class SingleRunnerMode {
        Plain,      ///< Run one by one operations.
        Benchmark,  ///< Run scenario by chunks with measurements.
        BenchmarkHashed,   ///< Same as benchmark + hashing results to avoid compiler optimizations.
    }; // SingleRunnerMode
        
    /// @brief Converts single runner mode to string (const char*).
    /// @param mode Target single runner mode.
    inline const char* mode_to_string(SingleRunnerMode mode) {
        switch(mode) {
            case SingleRunnerMode::Plain:               return "Plain";
            case SingleRunnerMode::Benchmark:           return "Benchmark";
            case SingleRunnerMode::BenchmarkHashed:     return "BenchmarkHashed";
            default:                                    return "Unknown";
        }
    }

    /// @brief Configuration for the SingleRunner class.
    /// @tparam Key Operation key type.
    /// @tparam Value Operation value type.
    template<typename Key, typename Value>
    struct SingleRunnerConfig {
        uint64_t seed{};   ///< Seed for scenario generator to make reproduction.
        SingleRunnerMode mode{SingleRunnerMode::Plain}; ///< Mode of the single runner.
        std::size_t chunk_size{1000000};    ///< Chunk size used by Benchmark and BenchmarkHashed modes.
        std::size_t chunk_count{1024};      ///< Reserve for logger records (optionally).
        
        std::string name{"container"};  ///< Name of running container.
    }; // SingleRunnerConfig

    /**
     * @brief Runs a scenario against a single container.
     * 
     * SingleRunner is intended for standalone execution and benchmarking without
     * comparing results against a reference container.
     * 
     * @tparam Wrapper Adapter type for the running container.
     * @tparam Container Container type used by the run.
     * @tparam Key Operation key type.
     * @tparam Value Operation value type.
     */
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

        /**
         * @brief Standart method to run scenario.
         * 
         * Starts logging and launch the mode that is specified in SingleRunnerConfig.
         */
        RunnerReport<Key, Value> run() { 
            start_meta("container", cfg_.name.c_str());

            switch(cfg_.mode) {
                case SingleRunnerMode::Plain: {
                    return run_plain();
                }
                case SingleRunnerMode::Benchmark: {
                    return run_benchmark();
                }
                case SingleRunnerMode::BenchmarkHashed: {
                    return run_benchmark_hashed();
                }
                default: {
                    throw std::runtime_error("Unknown SingleRunnerMode");
                }
            }
        }

    private:

        /// @brief Runs operations one by one without timing chunks.
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

        /// @brief Run operations by chunks with measurements. 
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

        /// @brief Runs operations in chunks, records measurements, and hashes operation results.
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

        /** 
         * @brief Add general metadata about launching.
         * 
         * @param type Key of metadata.
         * @param container_name Name of the container for logging.
         */
        void start_meta(const char* type, const char* container_name) {
            logger_.add_meta("type", type);
            logger_.add_meta("container_name", container_name);
            logger_.add_meta("seed", std::to_string(cfg_.seed));
            logger_.add_meta("mode", mode_to_string(cfg_.mode));
            logger_.add_meta("scenario", scenario_.to_string());
        }

        /// Configuration of SingleRunner.
        SingleRunnerConfig<Key, Value> cfg_;
        /// Scenario for launch.
        AbstractScenario<Key, Value>& scenario_;
        /// Logger for running container.
        Logger logger_;
        /// Fabric for building running container.
        std::function<Container()> build_;
    }; // class SingleRunner

} // namespace valid_framework
