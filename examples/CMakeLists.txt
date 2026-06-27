add_library(valid_framework_example_support INTERFACE)

target_include_directories(valid_framework_example_support
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/common
)

target_link_libraries(valid_framework_example_support
    INTERFACE
        valid_framework::valid_framework
)

add_subdirectory(benchmark)
add_subdirectory(validate)
add_subdirectory(single)
