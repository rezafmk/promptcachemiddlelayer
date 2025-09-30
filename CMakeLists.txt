cmake_minimum_required(VERSION 3.16)
project(KVCache LANGUAGES CXX C)

# Set the C++ standard to C++20 and require it
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Find Dependencies ---
# Find ZLIB first, as the AWS SDK may require it on some systems.
find_package(ZLIB REQUIRED)
# Find AWS SDK for C++
find_package(AWSSDK REQUIRED COMPONENTS s3)

# --- Library: kvcache ---
# Add xxhash.c directly to the library sources
add_library(kvcache STATIC
    src/api.cpp
    src/s3_client.cpp
    src/hash.cpp
    src/lru.cpp
    hash/xxhash/xxhash.c
)

target_include_directories(kvcache PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/hash/xxhash>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link the kvcache library to the AWS SDK.
# The AWSSDK_LIBRARIES variable will now correctly find the ZLIB::ZLIB target.
target_link_libraries(kvcache PRIVATE
    ${AWSSDK_LIBRARIES}
)

# --- Application: kvbench ---
add_executable(kvbench
    apps/bench/main.cpp
)

target_link_libraries(kvbench PRIVATE
    kvcache
)
