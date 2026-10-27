cmake_minimum_required(VERSION 3.20)
project(itchfeed CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# The benchmark flags. -g only adds debug info for perf annotate; it does not
# change the generated code.
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g -march=native")
add_compile_options(-Wall -Wextra)

include_directories(include)
add_executable(itchfeed src/itchfeed.cpp)
add_executable(itchbench src/itchbench.cpp)
add_executable(test_parser tests/test_parser.cpp)

enable_testing()
add_test(NAME parser COMMAND test_parser)
