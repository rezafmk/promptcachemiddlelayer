cmake_minimum_required(VERSION 2.8.12)
project(LMCache)

# --- Set C++ Standard ---
# For CMake < 3.1, we must set the standard via compiler flags.
# The code requires C++20.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a") # Use c++2a for older GCC/Clang

# --- Find Dependencies ---
# Note: The find module for AWSSDK might have its own minimum CMake version requirement.
# This script assumes it's compatible or that you have a version that works with older CMake.
find_package(AWSSDK REQUIRED COMPONENTS s3)
find_package(Threads REQUIRED)

# --- Project Options ---
option(BUILD_BENCHMARK "Build the synthetic benchmark application" ON)

# --- Include Directories (Global) ---
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xxhash
    ${AWSSDK_INCLUDE_DIRS}
)

# --- Library: kvcache ---
# List sources for the library
set(KVC_SOURCES
    src/api.cpp
    src/hash.cpp
    src/s3_client.cpp
    src/lru.cpp
    third_party/xxhash/xxhash.h
    third_party/xxhash/xxh3.h
)

add_library(kvcache STATIC ${KVC_SOURCES})

# Link libraries
# Use the old variable CMAKE_THREAD_LIBS_INIT instead of Threads::Threads
target_link_libraries(kvcache
    ${CMAKE_THREAD_LIBS_INIT}
    ${AWSSDK_LIBRARIES}
)

# --- Application: kvbench ---
if(BUILD_BENCHMARK)
    add_executable(kvbench apps/bench/main.cpp)
    target_link_libraries(kvbench kvcache)

    # Install the benchmark executable
    install(TARGETS kvbench DESTINATION bin)
endif()

# --- Installation ---
# Note: EXPORT is not supported in this old version in the same way.
# This provides a basic installation.
install(TARGETS kvcache
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

# --- Output Directories ---
# Set output paths for libraries and executables
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
