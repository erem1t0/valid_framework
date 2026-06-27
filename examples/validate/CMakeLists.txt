add_executable(validate_example validate_example.cpp)

target_link_libraries(validate_example
    PRIVATE
        valid_framework_example_support
)
