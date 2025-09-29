#pragma once

#include "types.hpp"
#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <string>
#include <vector>
#include <memory>
#include <span>

namespace kvcache {

class S3Client {
public:
    explicit S3Client(const Config& config);
    ~S3Client();

    bool GetObject(const std::string& key, std::vector<std::uint8_t>* out_bytes);
    bool PutObject(const std::string& key, std::span<const std::uint8_t> bytes);
    bool DeleteObject(const std::string& key);
    bool HeadObject(const std::string& key, std::uint64_t* out_size);

private:
    Aws::SDKOptions aws_sdk_options_;
    std::shared_ptr<Aws::S3::S3Client> client_;
    std::string bucket_;
};

} // namespace kvcache
