#include "kvcache/api.hpp"
#include "kvcache/types.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <numeric>
#include <atomic>
#include <iomanip>
#include <cstdlib> // For getenv

struct BenchConfig {
    int iterations = 50000;
    int threads = 8;
    int num_prefixes = 10000;
    double reuse_prob = 0.30;
    uint32_t block_size = 256;
    uint64_t avg_block_bytes = 1048576;
    kvcache::Config cache_config;
};

struct Stats {
    std::atomic<int> ops = 0;
    std::atomic<int> hits = 0;
    std::atomic<uint64_t> bytes_stored = 0;
    std::atomic<double> get_latency_ms = 0.0;
    std::atomic<double> put_latency_ms = 0.0;
};

void worker_thread(kvcache::KVCache& cache, const BenchConfig& config, Stats& stats, const std::vector<std::vector<unsigned int>>& prefixes, int thread_id) {
    std::mt19937 gen(thread_id);
    std::uniform_real_distribution<> dis(0.0, 1.0);
    std::uniform_int_distribution<> prefix_dist(0, prefixes.size() - 1);
    std::uniform_int_distribution<unsigned int> token_dist(0, 100000);

    for (int i = 0; i < config.iterations / config.threads; ++i) {
        std::vector<unsigned int> tokens;
        if (dis(gen) < config.reuse_prob) {
            tokens = prefixes[prefix_dist(gen)];
        } else {
            int num_blocks = std::uniform_int_distribution<>(1, 8)(gen);
            tokens.resize(num_blocks * config.block_size);
            for(auto& token : tokens) token = token_dist(gen);
        }

        auto lookup_res = cache.Lookup(tokens);
        stats.ops++;
        if (lookup_res.matched_tokens > 0) {
            stats.hits++;
        }

        size_t full_blocks = tokens.size() / config.block_size;
        size_t matched_blocks = lookup_res.matched_tokens / config.block_size;

        if (matched_blocks < full_blocks) {
            uint32_t block_index_to_store = matched_blocks;
            std::vector<uint8_t> block_bytes(config.avg_block_bytes);
            std::fill(block_bytes.begin(), block_bytes.end(), static_cast<uint8_t>(thread_id));
            
            auto start = std::chrono::high_resolution_clock::now();
            cache.Store(tokens, block_index_to_store, block_bytes);
            auto end = std::chrono::high_resolution_clock::now();
            stats.put_latency_ms += std::chrono::duration<double, std::milli>(end - start).count();
            stats.bytes_stored += block_bytes.size();
        }

        if (!lookup_res.handles.empty()) {
            int block_to_load_idx = std::uniform_int_distribution<int>(0, lookup_res.handles.size() - 1)(gen);
            std::vector<uint8_t> out_bytes;
            
            auto start = std::chrono::high_resolution_clock::now();
            cache.Load(lookup_res.handles[block_to_load_idx], &out_bytes);
            auto end = std::chrono::high_resolution_clock::now();
            stats.get_latency_ms += std::chrono::duration<double, std::milli>(end - start).count();
        }
    }
}

// Helper to get environment variables
std::string get_env(const char* name, const std::string& default_val = "") {
    const char* val = std::getenv(name);
    return val == nullptr ? default_val : std::string(val);
}

int main(int argc, char* argv[]) {
    BenchConfig bench_config;
    
    // Read S3 config from environment variables
    bench_config.cache_config.s3_endpoint = get_env("AWS_ENDPOINT_URL");
    bench_config.cache_config.s3_region = get_env("AWS_REGION");
    bench_config.cache_config.aws_access_key_id = get_env("AWS_ACCESS_KEY_ID");
    bench_config.cache_config.aws_secret_access_key = get_env("AWS_SECRET_ACCESS_KEY");

    // Simple argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) bench_config.iterations = std::stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc) bench_config.threads = std::stoi(argv[++i]);
        else if (arg == "--capacity-bytes" && i + 1 < argc) bench_config.cache_config.capacity_bytes = std::stoull(argv[++i]);
        else if (arg == "--s3-bucket" && i + 1 < argc) bench_config.cache_config.s3_bucket = argv[++i];
    }

    // --- Configuration Validation ---
    bool config_ok = true;
    if (bench_config.cache_config.s3_endpoint.empty()) {
        std::cerr << "Error: S3 endpoint not set. Please set the AWS_ENDPOINT_URL environment variable." << std::endl;
        config_ok = false;
    }
    if (bench_config.cache_config.s3_region.empty()) {
        std::cerr << "Error: S3 region not set. Please set the AWS_REGION environment variable." << std::endl;
        config_ok = false;
    }
    if (bench_config.cache_config.aws_access_key_id.empty()) {
        std::cerr << "Error: AWS access key not set. Please set the AWS_ACCESS_KEY_ID environment variable." << std::endl;
        config_ok = false;
    }
    if (bench_config.cache_config.aws_secret_access_key.empty()) {
        std::cerr << "Error: AWS secret key not set. Please set the AWS_SECRET_ACCESS_KEY environment variable." << std::endl;
        config_ok = false;
    }
    if (bench_config.cache_config.s3_bucket.empty()) {
        std::cerr << "Error: S3 bucket not specified. Please provide it using the --s3-bucket <name> argument." << std::endl;
        config_ok = false;
    }

    if (!config_ok) {
        return 1;
    }

    // Pre-generate prefixes
    std::cout << "Generating " << bench_config.num_prefixes << " prefixes..." << std::endl;
    std::vector<std::vector<unsigned int>> prefixes(bench_config.num_prefixes);
    std::mt19937 gen(0);
    std::uniform_int_distribution<unsigned int> token_dist(0, 100000);
    for (auto& p : prefixes) {
        int num_blocks = std::uniform_int_distribution<>(1, 8)(gen);
        p.resize(num_blocks * bench_config.block_size);
        for(auto& token : p) token = token_dist(gen);
    }

    kvcache::KVCache cache(bench_config.cache_config);
    std::vector<std::thread> threads;
    std::vector<Stats> stats(bench_config.threads);

    auto start_time = std::chrono::high_resolution_clock::now();

    std::cout << "Starting " << bench_config.threads << " threads for " << bench_config.iterations << " total iterations..." << std::endl;
    for (int i = 0; i < bench_config.threads; ++i) {
        threads.emplace_back(worker_thread, std::ref(cache), std::ref(bench_config), std::ref(stats[i]), std::ref(prefixes), i);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double duration_s = std::chrono::duration<double>(end_time - start_time).count();

    // Aggregate stats
    Stats total_stats;
    int total_gets = 0;
    int total_puts = 0;
    for (const auto& s : stats) {
        total_stats.ops += s.ops.load();
        total_stats.hits += s.hits.load();
        total_stats.bytes_stored += s.bytes_stored.load();
        total_stats.get_latency_ms += s.get_latency_ms.load();
        total_stats.put_latency_ms += s.put_latency_ms.load();
        
        int thread_ops = s.ops.load();
        if (thread_ops > 0) {
            double hit_ratio_thread = static_cast<double>(s.hits.load()) / thread_ops;
            int gets_in_thread = thread_ops * hit_ratio_thread;
            int puts_in_thread = thread_ops - gets_in_thread;
            if (gets_in_thread > 0) total_gets++;
            if (puts_in_thread > 0) total_puts++;
        }
    }
    
    double ops_per_sec = (duration_s > 0) ? total_stats.ops / duration_s : 0.0;
    double hit_ratio = (total_stats.ops > 0) ? static_cast<double>(total_stats.hits) / total_stats.ops : 0.0;
    double avg_get_latency = (total_gets > 0) ? total_stats.get_latency_ms / total_gets : 0.0;
    double avg_put_latency = (total_puts > 0) ? total_stats.put_latency_ms / total_puts : 0.0;

    std::cout << "\n--- Results ---" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total duration: " << duration_s << " s" << std::endl;
    std::cout << "Ops/sec: " << ops_per_sec << std::endl;
    std::cout << "Hit ratio: " << hit_ratio * 100.0 << "%" << std::endl;
    std::cout << "Bytes stored: " << total_stats.bytes_stored / (1024.0 * 1024.0) << " MiB" << std::endl;
    std::cout << "Average GET latency: " << avg_get_latency << " ms" << std::endl;
    std::cout << "Average PUT latency: " << avg_put_latency << " ms" << std::endl;
    std::cout << "Final used bytes: " << cache.UsedBytes() / (1024.0 * 1024.0) << " MiB / "
              << cache.CapacityBytes() / (1024.0 * 1024.0) << " MiB" << std::endl;

    return 0;
}
