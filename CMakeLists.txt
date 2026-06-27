cmake_minimum_required(VERSION 3.16)

project(valid_framework
    VERSION 1.0.0
    LANGUAGES CXX
)

option(VALID_FRAMEWORK_BUILD_EXAMPLES "Build valid_framework examples" OFF)
option(VALID_FRAMEWORK_BUILD_DOCS "Build valid_framework documentation" OFF)

add_library(valid_framework INTERFACE)

target_compile_features(valid_framework INTERFACE cxx_std_20)

target_include_directories(valid_framework
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_library(valid_framework::valid_framework ALIAS valid_framework)

if(VALID_FRAMEWORK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
