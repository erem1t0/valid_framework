#include <valid_framework/valid_framework.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <filesystem>

#include <set>
#include <UnknownContainer.hpp>

#include <StdSetWrapper.hpp>
#include <UnknownContainerWrapper.hpp>


/**
 * Validate scenario has 3 phases:
 * 
 * 1) Insert N random elements
 * 
 * 2) Get N/2 random existing elements
 * 
 * 3) Erase N/2 random existing elements
 * 
 * HitRate used by default (0.0 for insert, 1.0 for get and erase)
 */
template<typename TestWrapper,  typename TestContainer,
         typename ValidWrapper, typename ValidContainer,
         typename Key, typename Value>
valid_framework::RunnerReport<Key, Value> validate_scenario(
            std::size_t N,
            std::uint64_t seed,
            std::string test_name, 
            std::string valid_name, 
            valid_framework::RunnerMode runner_mode) {
    using namespace valid_framework;

    OpWeights phase1_weights { .get     = 100, };
    OpWeights phase2_weights { .insert  = 100, };
    OpWeights phase3_weights { .erase   = 100, };

    StatefulPhaseConfig phase1 {
        .ops_count = N,
        .gen_cfg = {
            .weights = phase1_weights,
        }, };

    StatefulPhaseConfig phase2 {
        .ops_count = N / 2,
        .gen_cfg = {
            .weights = phase2_weights,
        }, };

    StatefulPhaseConfig phase3 {
        .ops_count = N / 2,
        .gen_cfg = {
            .weights = phase3_weights,
        }, };

    // Calculate chunk_size to have 10 chunks_count
    const std::size_t chunk_size = (phase1.ops_count + phase2.ops_count + phase3.ops_count) / 10;

    std::filesystem::path logs_path = "logs";
    std::filesystem::create_directories(logs_path); 

    // Since std::set is used, then use SetKeyGenerator
    SetKeyGenerator<Key, Value> key_generator{};
    std::vector<StatefulPhaseConfig> phases = { phase1, phase2, phase3 };
    
    // Since 3 phases and 1 generator is used, then use StatefulPhasedScenario
    // SmartOperationGenerator is used to generate existing keys for get and erase operations
    StatefulPhasedScenario<Key, Value> phased_scenario(
        std::make_unique<SmartOperationGenerator<Key,Value>>(phase1.gen_cfg, key_generator), phases);

    RunnerConfig<Key, Value> runner_config{
        .seed = seed,
        .mode = runner_mode,
        .chunk_size = chunk_size,
        .chunk_count = (phase1.ops_count + phase2.ops_count + phase3.ops_count) / chunk_size, 
        .valid_name = valid_name,
        .test_name = test_name,
    }; 

    Runner<TestWrapper, TestContainer, 
            ValidWrapper, ValidContainer,   
            Key, Value> 
    runner(phased_scenario,
            (logs_path / "test.json").string(),
            (logs_path / "valid.json").string(),
            runner_config);

    auto report = runner.run();

    return report;
}


int main() {
    using Key = int;
    using Value = int;
    using namespace valid_framework;

    SetWrapper<Key, Value> test_wrapper;
    UnknownContainerWrapper<Key, Value> valid_wrapper;

    auto report = validate_scenario<SetWrapper<Key, Value>, std::set<Key>,
                                    UnknownContainerWrapper<Key, Value>, UnknownContainer<Key, Value>,
                                    Key, Value>(
                                        10000,
                                        42,
                                        "std::set",
                                        "UnknownContainer",
                                        RunnerMode::Validate);
    

    std::cout << "RESULTS:\n";
    std::cout << "Completed operations: " << report.completed_ops << "\n";
    std::cout << "Failed operations: " << report.failed_ops << "\n";
    if(report.failed_ops > 0) {
        std::cout << "Error message: " << report.message << "\n";
    }

    return 0;
}