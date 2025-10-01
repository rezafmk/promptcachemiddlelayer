#pragma once

#include "types.hpp"
#include "span_compat.hpp"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace Aws {
    namespace S3 {
        class S3Client;
    }
}

namespace kvcache {

class S3Client {
public:
    explicit S3Client(const Config& cfg);
    ~S3Client();

    bool GetObject(const std::string& key, std::vector<std::uint8_t>* data);
    bool PutObject(const std::string& key, bytes_view data);
    bool DeleteObject(const std::string& key);

private:
    struct S3ClientImpl;
    std::unique_ptr<S3ClientImpl> p_impl;
};

} // namespace kvcache
