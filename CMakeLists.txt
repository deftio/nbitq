# nbitq -- pack and unpack n-bit integers in a bit stream.
#
# This file works two ways:
#
#   standalone   cmake -S . -B build && cmake --build build && ctest --test-dir build
#   vendored     add_subdirectory(nbitq)  from a parent project
#
# nbitq has no dependency on anything, and nothing may leak into it from a
# parent, so a vendored copy is a plain copy of this directory. triepack
# (github.com/deftio/triepack) uses nbitq this way. There is also a plain
# Makefile for building and testing with no CMake at all.

cmake_minimum_required(VERSION 3.16)

# Only declare a project when built standalone; a parent has its own.
if(NOT DEFINED PROJECT_NAME)
    project(nbitq VERSION 0.2.0 LANGUAGES C CXX)
    set(NBITQ_STANDALONE TRUE)
endif()

# Tests are on by default standalone, and follow the parent's choice when
# vendored: NBITQ_BUILD_TESTS if set, else the parent's generic BUILD_TESTS.
if(DEFINED NBITQ_BUILD_TESTS)
    set(_nbitq_tests ${NBITQ_BUILD_TESTS})
elseif(DEFINED BUILD_TESTS)
    set(_nbitq_tests ${BUILD_TESTS})
else()
    set(_nbitq_tests ON)
endif()

add_library(nbitq STATIC nbitq.c nbitq_container.c)
set_target_properties(nbitq PROPERTIES PUBLIC_HEADER "nbitq.h;nbitq.hpp;nbitq_container.h")
target_include_directories(nbitq PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(nbitq PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)

if(NBITQ_STANDALONE)
    target_compile_options(nbitq PRIVATE -Wall -Wextra -pedantic)
endif()

if(_nbitq_tests)
    if(NBITQ_STANDALONE)
        enable_testing()
    endif()

    add_executable(nbitq_test test_nbitq.c)
    target_link_libraries(nbitq_test PRIVATE nbitq)
    add_test(NAME nbitq COMMAND nbitq_test)
    set_tests_properties(nbitq PROPERTIES LABELS "nbitq" ENVIRONMENT "ASAN_OPTIONS=allocator_may_return_null=1")

    add_executable(nbitq_container_test test_nbitq_container.c)
    target_link_libraries(nbitq_container_test PRIVATE nbitq)
    add_test(NAME nbitq_container COMMAND nbitq_container_test)
    set_tests_properties(nbitq_container PROPERTIES LABELS "nbitq")

    # Both headers claim extern "C" and nbitq.hpp claims to be a thin forwarder;
    # compiling, linking and running them from C++ is the only proof. The tests
    # ask the allocator for sizes it must refuse, which ASan allows only when
    # told to return NULL like a normal allocator.
    add_executable(nbitq_cxx test_nbitq_cxx.cpp)
    target_link_libraries(nbitq_cxx PRIVATE nbitq)
    add_test(NAME nbitq_cxx COMMAND nbitq_cxx)
    set_tests_properties(nbitq_cxx PROPERTIES LABELS "nbitq" ENVIRONMENT "ASAN_OPTIONS=allocator_may_return_null=1")

    # The same harness libFuzzer uses, over a fixed-seed PRNG so it runs on
    # any toolchain. Apple's clang ships no libFuzzer.
    add_executable(nbitq_fuzz_standalone fuzz_nbitq.c)
    target_compile_definitions(nbitq_fuzz_standalone PRIVATE NBQ_FUZZ_STANDALONE)
    target_link_libraries(nbitq_fuzz_standalone PRIVATE nbitq)
    add_test(NAME nbitq_fuzz COMMAND nbitq_fuzz_standalone 50000)
    set_tests_properties(nbitq_fuzz PROPERTIES LABELS "nbitq;fuzz" TIMEOUT 300)

    # The allocator is optional; prove the NBQ_NO_MALLOC configuration still
    # builds, since that is the one embedded targets use.
    add_library(nbitq_nomalloc STATIC nbitq.c)
    target_include_directories(nbitq_nomalloc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(nbitq_nomalloc PRIVATE NBQ_NO_MALLOC)
endif()
