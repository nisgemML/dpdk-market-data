cmake_minimum_required(VERSION 3.20)
project(dpdk_market_data CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED IMPORTED_TARGET libdpdk)
find_package(Threads REQUIRED)

add_library(dpdkmd_iface INTERFACE)
target_include_directories(dpdkmd_iface INTERFACE include third_party/mpsc-queue/include)
target_link_libraries(dpdkmd_iface INTERFACE PkgConfig::DPDK Threads::Threads)
# DPDK's own headers trip -Wvolatile under C++20 (not this repo's code) --
# suppressed only for that, not blanket-disabled for anything written here.
target_compile_options(dpdkmd_iface INTERFACE -Wall -Wextra -Wno-volatile)

add_executable(test_dpdk_primitives tests/test_dpdk_primitives.cpp)
target_link_libraries(test_dpdk_primitives PRIVATE dpdkmd_iface)
# Not registered via add_test()/ctest: DPDK's EAL needs hugepages reserved
# and a hugetlbfs mount *before* the process starts (see README.md/CI) --
# ctest has no hook for that setup, so this is invoked directly by CI and
# by run_tests.sh instead of through the ctest driver.

add_executable(bench_ring_comparison bench/bench_ring_comparison.cpp)
target_link_libraries(bench_ring_comparison PRIVATE dpdkmd_iface)
