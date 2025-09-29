#include <kvcache/api.hpp>
#include <kvcache/s3_settings.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <random>
#include <chrono>
#include <atomic>
#include <numeric>
#include <iomanip>

// --- Simple CLI Parser ---
// (A simple CLI parsing implementation would go here)
// For brevity, we will manually set the config in main.

struct BenchConfig {
    long long iterations = 50000;
    int threads = 8;
    int num_prefixes = 10000;
    double reuse_prob = 0.30;
    kvcache::Config kv_config;
    long long avg_block_bytes = 1048576;
};

struct Stats {
    std::atomic<long long> lookups{0};
    std::atomic<long long> stores{0};
    std::atomic<long long> loads{0};
    std::atomic<long long> hits{0};
    std::atomic<long long> bytes_stored{0};
    std::atomic<long long> evictions{0}; // Note: cannot be easily tracked from outside

    // Basic latency tracking
    std::atomic<long long> total_lookup_us{0};
    std::atomic<long long> total_store_us{0};
    std::atomic<long long> total_load_us{0};
};

// --- Main Benchmark Logic ---

void worker_thread(kvcache::KVCache& cache, const BenchConfig& cfg, Stats& stats,
                   const std::vector<std::vector<std::uint32_t>>& prefix_library,
                   int worker_id) {
    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count() + worker_id);
    std::uniform_real_distribution<> prob_dist(0.0, 1.0);
    std::uniform_int_distribution<> prefix_dist(0, cfg.num_prefixes - 1);
    std::uniform_int_distribution<std::uint32_t> token_dist(0, 50000);
    std::normal_distribution<> size_dist(cfg.avg_block_bytes, cfg.avg_block_bytes / 4.0);

    for (long long i = 0; i < cfg.iterations / cfg.threads; ++i) {
        // 1. Get a prefix
        std::vector<std::uint32_t> tokens;
        if (prob_dist(rng) < cfg.reuse_prob) {
            tokens = prefix_library[prefix_dist(rng)];
        } else {
            std::uniform_int_distribution<> len_dist(1, 8);
            int num_blocks = len_dist(rng);
            tokens.resize(num_blocks * cfg.kv_config.block_size_tokens);
            std::generate(tokens.begin(), tokens.end(), [&]() { return token_dist(rng); });
        }

        // 2. Lookup
        auto start_lookup = std::chrono::high_resolution_clock::now();
        auto lookup_res = cache.Lookup(tokens);
        auto end_lookup = std::chrono::high_resolution_clock::now();
        stats.total_lookup_us += std::chrono::duration_cast<std::chrono::microseconds>(end_lookup - start_lookup).count();
        stats.lookups++;

        if (lookup_res.matched_tokens > 0) {
            stats.hits++;
        }

        // 3. Store next block if not a full match
        std::uint32_t full_blocks = tokens.size() / cfg.kv_config.block_size_tokens;
        std::uint32_t matched_blocks = lookup_res.matched_tokens / cfg.kv_config.block_size_tokens;

        if (matched_blocks < full_blocks) {
            std::uint32_t block_to_store = matched_blocks;
            long long bytes_size = std::max(1.0, size_dist(rng));
            std::vector<std::uint8_t> block_data(bytes_size);
            std::generate(block_data.begin(), block_data.end(), [&]() { return rng() % 256; });

            auto start_store = std::chrono::high_resolution_clock::now();
            cache.Store(tokens, block_to_store, block_data);
            auto end_store = std::chrono::high_resolution_clock::now();
            stats.total_store_us += std::chrono::duration_cast<std::chrono::microseconds>(end_store - start_store).count();
            stats.stores++;
            stats.bytes_stored += bytes_size;
        }

        // 4. Load a random available block
        if (matched_blocks > 0) {
            std::uniform_int_distribution<> block_idx_dist(0, matched_blocks - 1);
            int block_to_load_idx = block_idx_dist(rng);
            std::vector<std::uint8_t> loaded_bytes;
            
            auto start_load = std::chrono::high_resolution_clock::now();
            cache.Load(lookup_res.handles[block_to_load_idx], &loaded_bytes);
            auto end_load = std::chrono::high_resolution_clock::now();
            stats.total_load_us += std::chrono::duration_cast<std::chrono::microseconds>(end_load - start_load).count();
            stats.loads++;
        }
    }
}

int main(int argc, char* argv[]) {
    // In a real app, parse argc/argv here. For this example, we use defaults.
    BenchConfig bench_cfg;
    // ... parsing logic would update bench_cfg ...

    // --- Setup ---
    std::cout << "--- KVCache Benchmark ---" << std::endl;
    std::cout << "Iterations: " << bench_cfg.iterations << ", Threads: " << bench_cfg.threads << std::endl;
    std::cout << "Capacity: " << (bench_cfg.kv_config.capacity_bytes / (1024*1024)) << " MiB" << std::endl;

    kvcache::KVCache cache(bench_cfg.kv_config);
    
    std::cout << "Generating " << bench_cfg.num_prefixes << " prefixes..." << std::endl;
    std::vector<std::vector<std::uint32_t>> prefix_library;
    prefix_library.reserve(bench_cfg.num_prefixes);
    std::mt19937 lib_rng(12345); // deterministic seed for library
    std::uniform_int_distribution<> len_dist(1, 8);
    std::uniform_int_distribution<std::uint32_t> token_dist(0, 50000);
    for (int i = 0; i < bench_cfg.num_prefixes; ++i) {
        int num_blocks = len_dist(lib_rng);
        std::vector<std::uint32_t> tokens(num_blocks * bench_cfg.kv_config.block_size_tokens);
        std::generate(tokens.begin(), tokens.end(), [&]() { return token_dist(lib_rng); });
        prefix_library.push_back(tokens);
    }
    
    Stats stats;
    std::vector<std::thread> threads;

    // --- Run ---
    auto start_time = std::chrono::high_resolution_clock::now();
    std::cout << "Starting benchmark..." << std::endl;

    for (int i = 0; i < bench_cfg.threads; ++i) {
        threads.emplace_back(worker_thread, std::ref(cache), std::cref(bench_cfg), std::ref(stats), i);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_s = std::chrono::duration<double>(end_time - start_time).count();

    // --- Report ---
    std::cout << "\n--- Benchmark Results ---" << std::endl;
    double total_ops = stats.lookups + stats.stores + stats.loads;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total duration: " << duration_s << " s" << std::endl;
    std::cout << "Ops/sec: " << total_ops / duration_s << std::endl;
    std::cout << "Hit Ratio: " << (stats.lookups > 0 ? (double)stats.hits / stats.lookups * 100.0 : 0.0) << "%" << std::endl;
    std::cout << "Total Bytes Stored: " << (double)stats.bytes_stored / (1024*1024) << " MiB" << std::endl;
    std::cout << "Final Used Bytes: " << (double)cache.UsedBytes() / (1024*1024) << " MiB" << std::endl;

    std::cout << "\nLatencies (avg):" << std::endl;
    std::cout << "  Lookup: " << (stats.lookups > 0 ? (double)stats.total_lookup_us / stats.lookups : 0.0) << " us" << std::endl;
    std::cout << "  Store: " << (stats.stores > 0 ? (double)stats.total_store_us / stats.stores : 0.0) << " us" << std::endl;
    std::cout << "  Load: " << (stats.loads > 0 ? (double)stats.total_load_us / stats.loads : 0.0) << " us" << std::endl;

    return 0;
}