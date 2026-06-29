add_executable(include_check include_check.cpp)

target_link_libraries(include_check
    PRIVATE
        valid_framework::valid_framework
)

add_test(
    NAME valid_framework.include_check
    COMMAND include_check
)

add_executable(valid_framework_concept_check concept_check.cpp)

target_link_libraries(valid_framework_concept_check
    PRIVATE
        valid_framework::valid_framework
)

add_test(
    NAME valid_framework.concept_check
    COMMAND valid_framework_concept_check
)
