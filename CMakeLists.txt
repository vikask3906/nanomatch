cmake_minimum_required(VERSION 3.16)
project(nanomatch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build type defaults ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops -fomit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

# ── Engine library (shared between main, tests, bench) ────────────────────────
add_library(engine_lib STATIC
  src/order_book.cpp
)
target_include_directories(engine_lib PUBLIC include)
target_compile_options(engine_lib PRIVATE -Wall -Wextra)

# ── Main binary ───────────────────────────────────────────────────────────────
add_executable(nanomatch src/main.cpp)
target_link_libraries(nanomatch PRIVATE engine_lib pthread)

# ── Unit tests ────────────────────────────────────────────────────────────────
find_package(GTest QUIET)
if(GTest_FOUND)
  enable_testing()
  add_executable(tests tests/test_matching.cpp)
  target_link_libraries(tests PRIVATE engine_lib GTest::gtest_main pthread)
  add_test(NAME matching_tests COMMAND tests)
  message(STATUS "GTest found — tests target enabled")
else()
  message(WARNING "GTest not found — skipping tests target. Install: sudo apt install libgtest-dev")
endif()

# ── Benchmarks ────────────────────────────────────────────────────────────────
find_package(benchmark QUIET)
if(benchmark_FOUND)
  add_executable(bench bench/bench_engine.cpp)
  target_link_libraries(bench PRIVATE engine_lib benchmark::benchmark pthread)
  message(STATUS "Google Benchmark found — bench target enabled")
else()
  message(WARNING "Google Benchmark not found — skipping bench target. Install: sudo apt install libbenchmark-dev")
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX flags  : ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
