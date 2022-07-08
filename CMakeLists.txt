cmake_minimum_required(VERSION 3.10)
project(tlsf_resource LANGUAGES CXX)

option(ENABLE_TESTING "Build unit tests for TLSF" OFF)

include(cmake/CompilerWarnings.cmake)
include(cmake/Sanitizers.cmake)

add_library(
    tlsf_resource 
    STATIC 
    src/tlsf_resource.cpp
    src/pool.cpp
    src/block.cpp
    )

target_compile_features(tlsf_resource PUBLIC cxx_std_17)

set_project_warnings(tlsf_resource)
enable_sanitizers(tlsf_resource)


target_include_directories(
    tlsf_resource
    PUBLIC
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>"
)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND ENABLE_TESTING)
include(CTest)
add_subdirectory(test)
endif()

