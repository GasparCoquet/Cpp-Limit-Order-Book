cmake_minimum_required(VERSION 3.10)
project(LimitOrderBook VERSION 1.0.0 LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for optimization
if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -O3)
endif()

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/OrderBook.cpp
)

# Create library
add_library(orderbook_lib ${SOURCES})
target_include_directories(orderbook_lib PUBLIC include)

# Main executable
add_executable(orderbook src/main.cpp)
target_link_libraries(orderbook orderbook_lib)

# Optional: Enable testing
enable_testing()
# Verification tests
add_executable(verify tests/verification_tests.cpp)
target_link_libraries(verify orderbook_lib)
add_test(NAME verification COMMAND verify)

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
