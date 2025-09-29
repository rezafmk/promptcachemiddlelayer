#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kvcache {

struct BlockRef {
    std::string s3_key;
    std::uint64_t size;
    std::uint32_t index;
};

struct LookupResult {
    std::uint32_t matched_tokens;
    std::vector<BlockRef> handles;
};

struct Config {
    std::string model_id = "demo-model";
    std::uint32_t block_size_tokens = 256;
    std::uint64_t capacity_bytes = 10ull * 1024 * 1024 * 1024; // 10 GiB

    // S3 Configuration
    std::string s3_endpoint;
    std::string s3_region;
    std::string s3_bucket;
    std::string aws_access_key_id;
    std::string aws_secret_access_key;
    bool s3_use_path_style = true;
};

} // namespace kvcache