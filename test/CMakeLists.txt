cmake_minimum_required(VERSION 3.10)
project(tlsf_resource_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)


enable_testing()

add_executable(
    tlsf_test
    test_tlsf_resource.cpp
    test_block.cpp
    test_pool.cpp
    )


target_link_libraries(
    tlsf_test
    tlsf_resource
    gtest_main
    )

# message(STATUS "Enabling sanitizers for tlsf_test")
target_compile_options(tlsf_test INTERFACE -g -fsanitize=memory)
        
include(GoogleTest)
gtest_discover_tests(tlsf_test)
