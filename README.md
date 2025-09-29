# TempCache: A Simple In-Process KV Cache for LLMs

This project provides a minimal C++20 library that implements a simple, in-process key-value cache middle layer for Large Language Model (LLM) KV blocks. It's designed to interface directly with an S3-compatible object store.

The core of the project is a static library, `kvcache`, and a synthetic benchmark application, `kvbench`, to demonstrate and test its functionality.

## Features

-   **Direct S3 Integration**: Uses the AWS SDK for C++ to store KV blocks as S3 objects.
-   **Prefix-Based Caching**: Caches sequences of tokens by identifying the longest available prefix.
-   **XXH3 Hashing**: Computes a 128-bit `PrefixKey` for token sequences using the fast XXH3 algorithm.
-   **LRU Eviction**: A background garbage collection thread manages cache capacity by evicting the least-recently-used blocks from S3.
-   **Thread-Safe**: Designed for concurrent access from multiple threads.
-   **Configurable**: Cache behavior and S3 endpoints are configurable at runtime.
-   **Synthetic Benchmark**: A tool to simulate a workload and measure performance metrics like hit ratio and throughput.

## Project Structure

```
.
├── apps
│   └── bench
│       └── main.cpp            # Synthetic benchmark application
├── CMakeLists.txt              # Main CMake build script
├── include
│   └── kvcache
│       ├── api.hpp             # Public API (KVCache class)
│       ├── hash.hpp            # Hashing and encoding helpers
│       ├── lru.hpp             # LRU list implementation
│       ├── s3_client.hpp       # S3 client wrapper
│       ├── s3_settings.hpp     # Compile-time S3 configuration
│       └── types.hpp           # Core data structures (Config, BlockRef, etc.)
├── README.md                   # This file
├── src
│   ├── api.cpp
│   ├── hash.cpp
│   ├── lru.cpp
│   └── s3_client.cpp
└── third_party
    └── xxhash                  # Vendored xxHash library
        ├── LICENSE
        ├── xxh3.h
        └── xxhash.h
```

## Prerequisites

1.  **C++20 Compiler**: A modern C++ compiler (e.g., GCC 10+, Clang 12+).
2.  **CMake**: Version 3.16 or higher.
3.  **AWS SDK for C++**: The library requires the S3 and Core components of the AWS SDK.

### Installing AWS SDK for C++ (Ubuntu/Debian Example)

You can build the SDK from source. This example shows a minimal build.

```bash
# Install build dependencies
sudo apt-get update
sudo apt-get install -y git build-essential cmake libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev

# Clone the repository
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp.git
cd aws-sdk-cpp

# Create a build directory
mkdir build && cd build

# Configure and build only the necessary components
# Note: Adjust -DCMAKE_INSTALL_PREFIX if you want to install it elsewhere
cmake .. -DBUILD_ONLY="s3;core" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCUSTOM_MEMORY_MANAGEMENT=OFF
cmake --build . --parallel $(nproc)
sudo cmake --install .

# Update library cache
sudo ldconfig
```
This will install the static libraries and headers to `/usr/local/`.

## Building the Project

Once the dependencies are installed, you can build the project using CMake.

```bash
# Clone this repository
# git clone ...
# cd lmcache

# Configure the build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build the project
cmake --build build -j
```

This will produce two main artifacts:
-   `build/lib/libkvcache.a`: The static library.
-   `build/apps/bench/kvbench`: The benchmark executable.

## Configuration

The cache and S3 client can be configured in three ways, in order of precedence:

1.  **Command-line arguments** (for the benchmark).
2.  **Environment variables**.
3.  **Compile-time constants** in `include/kvcache/s3_settings.hpp`.

### S3 Configuration

| Setting                 | CLI Flag (Benchmark)        | Environment Variable         | Default (s3_settings.hpp)      |
| ----------------------- | --------------------------- | ---------------------------- | ------------------------------ |
| S3 Endpoint URL         | `--s3-endpoint`             | `KVC_S3_ENDPOINT`            | `http://127.0.0.1:9000`        |
| S3 Region               | `--s3-region`               | `KVC_S3_REGION`              | `us-east-1`                    |
| S3 Bucket               | `--s3-bucket`               | `KVC_S3_BUCKET`              | `kv-cache`                     |
| AWS Access Key ID       | `--aws-access-key-id`       | `KVC_AWS_ACCESS_KEY_ID`      | `minioadmin`                   |
| AWS Secret Access Key   | `--aws-secret-access-key`   | `KVC_AWS_SECRET_ACCESS_KEY`  | `minioadmin`                   |
| S3 Path Style Addressing| `--s3-use-path-style` (bool)| `KVC_S3_USE_PATH_STYLE` (0/1)| `true`                         |

**Note**: For local testing, you can use a MinIO server. The default settings are configured for a standard local MinIO instance.

## Running the Benchmark

The `kvbench` application simulates a workload to test the cache's performance.

```bash
# Example run against a local MinIO instance
./build/apps/bench/kvbench \
    --iterations 50000 \
    --threads 8 \
    --reuse-prob 0.3 \
    --capacity-bytes 10737418240 # 10 GiB
```

### Benchmark CLI Options

-   `--iterations`: Total number of operations to perform.
-   `--threads`: Number of worker threads.
-   `--num-prefixes`: Size of the pre-generated prefix library.
-   `--reuse-prob`: Probability of reusing a prefix from the library (0.0 to 1.0).
-   `--block-size`: Number of tokens per block.
-   `--avg-block-bytes`: Average size in bytes for randomly generated blocks.
-   `--capacity-bytes`: Total cache capacity in bytes.
-   S3 configuration flags (see table above).

The benchmark will print live statistics and a final summary of operations per second, hit ratio, latencies, and more.
