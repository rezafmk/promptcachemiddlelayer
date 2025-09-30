#pragma once

#include "kvcache/config.hpp"
#include <aws/s3/S3Client.h>
#include <memory>
#include <string>
#include <vector>
#include <span>

namespace kvcache {

class S3Client {
public:
    S3Client(const Config& config);
    ~S3Client();

    bool PutObject(const std::string& key, std::span<const std::uint8_t> data);
    bool GetObject(const std::string& key, std::vector<std::uint8_t>* out_bytes);
    bool DeleteObject(const std::string& key);

private:
    std::string bucket_name_;
    std::unique_ptr<Aws::S3::S3Client> client_;
};

} // namespace kvcache
