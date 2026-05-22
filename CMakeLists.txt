cmake_minimum_required(VERSION 3.16)
project(nanomatch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Build type defaults ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
if(MSVC)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG   "/Od /Zi")
  add_compile_options(/W4 /EHsc)
else()
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops -fomit-frame-pointer")
  set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")
  add_compile_options(-Wall -Wextra)
endif()

# ── Engine library (shared between main, tests, bench) ────────────────────────
add_library(engine_lib STATIC
  src/order_book.cpp
)
target_include_directories(engine_lib PUBLIC include)

# ── Main binary (threaded: engine + SPSC logger) ─────────────────────────────
add_executable(nanomatch src/main.cpp)
target_link_libraries(nanomatch PRIVATE engine_lib)
if(NOT MSVC)
  target_link_libraries(nanomatch PRIVATE pthread)
endif()

# ── Simple binary (single-threaded, no logger) ───────────────────────────────
add_executable(nanomatch_simple src/main_simple.cpp)
target_link_libraries(nanomatch_simple PRIVATE engine_lib)

# ── Standalone tests (no external deps) ───────────────────────────────────────
add_executable(tests_standalone tests/test_matching_standalone.cpp)
target_link_libraries(tests_standalone PRIVATE engine_lib)
enable_testing()
add_test(NAME standalone_tests COMMAND tests_standalone)

# ── Standalone benchmark (optimized vs naive, no external deps) ───────────────
add_executable(bench_standalone bench/bench_standalone.cpp)
target_link_libraries(bench_standalone PRIVATE engine_lib)

# ── Unit tests (optional — needs GTest) ───────────────────────────────────────
find_package(GTest QUIET)
if(GTest_FOUND)
  add_executable(tests tests/test_matching.cpp)
  target_link_libraries(tests PRIVATE engine_lib GTest::gtest_main)
  if(NOT MSVC)
    target_link_libraries(tests PRIVATE pthread)
  endif()
  add_test(NAME matching_tests COMMAND tests)
  message(STATUS "GTest found — tests target enabled")
else()
  message(STATUS "GTest not found — using tests_standalone only. Install: sudo apt install libgtest-dev")
endif()

# ── Benchmarks (optional — needs Google Benchmark) ────────────────────────────
find_package(benchmark QUIET)
if(benchmark_FOUND)
  add_executable(bench bench/bench_engine.cpp)
  target_link_libraries(bench PRIVATE engine_lib benchmark::benchmark)
  if(NOT MSVC)
    target_link_libraries(bench PRIVATE pthread)
  endif()
  message(STATUS "Google Benchmark found — bench target enabled")
else()
  message(STATUS "Google Benchmark not found — using bench_standalone only. Install: sudo apt install libbenchmark-dev")
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "Build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler          : ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Targets           : nanomatch, nanomatch_simple, tests_standalone, bench_standalone")
if(GTest_FOUND)
  message(STATUS "                  + tests (GTest)")
endif()
if(benchmark_FOUND)
  message(STATUS "                  + bench (Google Benchmark)")
endif()
