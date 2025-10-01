#include "kvcache/api.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <thread>
#include <atomic>
#include <chrono>
#include <numeric>
#include "kvcache/cxxopts.hpp"

struct BenchConfig {
    int num_threads = 4;
    int num_prompts = 1000;
    int min_prompt_len = 512;
    int max_prompt_len = 2048;
    int block_size = 256;
    double get_ratio = 0.8;
};

struct Stats {
    std::atomic<int> num_puts{0};
    std::atomic<int> num_gets{0};
    std::atomic<int> cache_hits{0};
    std::atomic<double> put_latency_ms{0.0};
    std::atomic<double> get_latency_ms{0.0};
};

// Helper function to atomically add to a std::atomic<double>
// This is required for compilers that don't support fetch_add on double (pre-C++20)
void atomic_add_double(std::atomic<double>& atomic_double, double value) {
    double old_val = atomic_double.load();
    double new_val;
    do {
        new_val = old_val + value;
    } while (!atomic_double.compare_exchange_weak(old_val, new_val));
}

std::vector<std::uint32_t> generate_prompt(int len, std::mt19937& rng) {
    std::vector<std::uint32_t> tokens(len);
    std::uniform_int_distribution<std::uint32_t> dist(0, 50256); // vocab size
    for (int i = 0; i < len; ++i) {
        tokens[i] = dist(rng);
    }
    return tokens;
}

void worker_thread(kvcache::KVCache& cache,
                   const BenchConfig& cfg,
                   Stats& stats,
                   const std::vector<std::vector<std::uint32_t>>& prompts,
                   int thread_id) {
    std::mt19937 rng(thread_id);
    std::uniform_real_distribution<> op_dist(0.0, 1.0);
    std::uniform_int_distribution<int> prompt_dist(0, prompts.size() - 1);

    int ops_per_thread = cfg.num_prompts / cfg.num_threads;

    for (int i = 0; i < ops_per_thread; ++i) {
        const auto& tokens = prompts[prompt_dist(rng)];
        
        if (op_dist(rng) < cfg.get_ratio) {
            // GET operation
            auto start = std::chrono::high_resolution_clock::now();
            auto result = cache.Lookup(tokens);
            auto end = std::chrono::high_resolution_clock::now();
            
            stats.num_gets++;
            if (result.matched_tokens > 0) {
                stats.cache_hits++;
            }
            atomic_add_double(stats.get_latency_ms, std::chrono::duration<double, std::milli>(end - start).count());

        } else {
            // PUT operation
            std::uint32_t num_blocks = tokens.size() / cfg.block_size;
            if (num_blocks == 0) continue;

            std::vector<std::uint8_t> block_data(1024); // dummy data
            
            auto start = std::chrono::high_resolution_clock::now();
            for (std::uint32_t j = 0; j < num_blocks; ++j) {
                cache.Store(tokens, j, block_data);
            }
            auto end = std::chrono::high_resolution_clock::now();
            
            stats.num_puts++;
            atomic_add_double(stats.put_latency_ms, std::chrono::duration<double, std::milli>(end - start).count());
        }
    }
}

int main(int argc, char** argv) {
    cxxopts::Options options("kvbench", "Benchmark tool for KVCache");
    options.add_options()
        ("t,threads", "Number of worker threads", cxxopts::value<int>()->default_value("4"))
        ("p,prompts", "Total number of operations", cxxopts::value<int>()->default_value("1000"))
        ("min-len", "Min prompt length", cxxopts::value<int>()->default_value("512"))
        ("max-len", "Max prompt length", cxxopts::value<int>()->default_value("2048"))
        ("b,block-size", "Block size in tokens", cxxopts::value<int>()->default_value("256"))
        ("g,get-ratio", "Ratio of GET operations (0.0 to 1.0)", cxxopts::value<double>()->default_value("0.8"))
        ("h,help", "Print usage");
    
    auto result = options.parse(argc, argv);

    if (result.count("help")) {
      std::cout << options.help() << std::endl;
      exit(0);
    }

    BenchConfig cfg;
    cfg.num_threads = result["threads"].as<int>();
    cfg.num_prompts = result["prompts"].as<int>();
    cfg.min_prompt_len = result["min-len"].as<int>();
    cfg.max_prompt_len = result["max-len"].as<int>();
    cfg.block_size = result["block-size"].as<int>();
    cfg.get_ratio = result["get-ratio"].as<double>();

    std::cout << "--- Benchmark Configuration ---" << std::endl;
    std::cout << "Threads: " << cfg.num_threads << std::endl;
    std::cout << "Total Ops: " << cfg.num_prompts << std::endl;
    std::cout << "Prompt Length: [" << cfg.min_prompt_len << ", " << cfg.max_prompt_len << "]" << std::endl;
    std::cout << "Block Size: " << cfg.block_size << std::endl;
    std::cout << "GET Ratio: " << cfg.get_ratio << std::endl;
    std::cout << "-----------------------------" << std::endl;

    // Generate a pool of prompts
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> len_dist(cfg.min_prompt_len, cfg.max_prompt_len);
    std::vector<std::vector<std::uint32_t>> prompts;
    for (int i = 0; i < 100; ++i) { // pool of 100 prompts
        prompts.push_back(generate_prompt(len_dist(rng), rng));
    }

    kvcache::Config kv_cfg;
    kv_cfg.block_size_tokens = cfg.block_size;
    kvcache::KVCache cache(kv_cfg);

    std::vector<std::thread> threads;
    std::vector<Stats> thread_stats(cfg.num_threads);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < cfg.num_threads; ++i) {
        threads.emplace_back(worker_thread, std::ref(cache), std::cref(cfg), std::ref(thread_stats[i]), std::cref(prompts), i);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    double total_duration_s = std::chrono::duration<double>(end_time - start_time).count();

    Stats total_stats;
    for (const auto& s : thread_stats) {
        total_stats.num_gets += s.num_gets.load();
        total_stats.num_puts += s.num_puts.load();
        total_stats.cache_hits += s.cache_hits.load();
        atomic_add_double(total_stats.get_latency_ms, s.get_latency_ms.load());
        atomic_add_double(total_stats.put_latency_ms, s.put_latency_ms.load());
    }

    double hit_rate = (total_stats.num_gets > 0) ? (double)total_stats.cache_hits / total_stats.num_gets * 100.0 : 0.0;
    double avg_get_latency = (total_stats.num_gets > 0) ? total_stats.get_latency_ms.load() / total_stats.num_gets : 0.0;
    double avg_put_latency = (total_stats.num_puts > 0) ? total_stats.put_latency_ms.load() / total_stats.num_puts : 0.0;
    double ops_per_sec = cfg.num_prompts / total_duration_s;

    std::cout << "----------- Results -----------" << std::endl;
    std::cout << "Total duration: " << total_duration_s << " s" << std::endl;
    std::cout << "Operations per second: " << ops_per_sec << std::endl;
    std::cout << "GET operations: " << total_stats.num_gets << std::endl;
    std::cout << "PUT operations: " << total_stats.num_puts << std::endl;
    std::cout << "Cache hit rate: " << hit_rate << " %" << std::endl;
    std::cout << "Avg. GET latency: " << avg_get_latency << " ms" << std::endl;
    std::cout << "Avg. PUT latency: " << avg_put_latency << " ms" << std::endl;
    std::cout << "-----------------------------" << std::endl;

    return 0;
}