cmake_minimum_required(VERSION 3.10)
project(http2-test)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# Include directories
include_directories(include)

# Main library source files
set(HTTP2_PARSER_SOURCES
    src/hpack.cpp
    src/header_parser.cpp
)

# Create library target
add_library(http2-parser ${HTTP2_PARSER_SOURCES})

# Enable testing
enable_testing()

# Include Google Test or Catch2
find_package(GTest CONFIG REQUIRED)

# Test source files
set(TEST_SOURCES
    test/test_hpack.cpp
    test/test_header_parser.cpp
)

# Create test executable
add_executable(http2-test-runner ${TEST_SOURCES})

# Link test executable with library and test framework
target_link_libraries(http2-test-runner PRIVATE http2-parser GTest::gtest GTest::gtest_main)

# Register tests
add_test(NAME http2-tests COMMAND http2-test-runner)
