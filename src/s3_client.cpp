#include "kvcache/s3_client.hpp"
#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <iostream>
#include <fstream>
#include <memory>

namespace kvcache {

// We manage SDK initialization globally and statically to ensure it's done only once.
class SdkManager {
public:
    SdkManager() {
        Aws::InitAPI(options_);
    }
    ~SdkManager() {
        Aws::ShutdownAPI(options_);
    }
private:
    Aws::SDKOptions options_;
};

static SdkManager sdk_manager;


S3Client::S3Client(const Config& config) : bucket_name_(config.s3_bucket) {
    Aws::Client::ClientConfiguration client_cfg;
    if (!config.s3_region.empty()) {
        client_cfg.region = config.s3_region;
    }
    if (!config.s3_endpoint.empty()) {
        client_cfg.endpointOverride = config.s3_endpoint;
    }
    client_cfg.scheme = Aws::Http::Scheme::HTTPS;
    if (!config.s3_endpoint.empty() && config.s3_endpoint.rfind("http://", 0) == 0) {
        client_cfg.scheme = Aws::Http::Scheme::HTTP;
    }
    
    Aws::Auth::AWSCredentials credentials(config.aws_access_key_id, config.aws_secret_access_key);
    
    s3_client_ = std::make_unique<Aws::S3::S3Client>(credentials, client_cfg,
        Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, config.s3_use_path_style);
}

S3Client::~S3Client() = default;

bool S3Client::PutObject(const std::string& key, std::span<const std::uint8_t> data) {
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(key);

    const auto input_data = Aws::MakeShared<Aws::StringStream>("S3Client");
    input_data->write(reinterpret_cast<const char*>(data.data()), data.size());
    request.SetBody(input_data);

    auto outcome = s3_client_->PutObject(request);
    if (!outcome.IsSuccess()) {
        std::cerr << "S3 PutObject error: " << outcome.GetError().GetMessage() << std::endl;
    }
    return outcome.IsSuccess();
}

bool S3Client::GetObject(const std::string& key, std::vector<std::uint8_t>* out_bytes) {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(key);

    auto outcome = s3_client_->GetObject(request);
    if (!outcome.IsSuccess()) {
        // It's common for Get to fail if the object doesn't exist, so don't always log as an error.
        return false;
    }

    auto& body = outcome.GetResult().GetBody();
    out_bytes->assign(std::istreambuf_iterator<char>(body), {});
    return true;
}

bool S3Client::DeleteObject(const std::string& key) {
    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(bucket_name_);
    request.SetKey(key);

    auto outcome = s3_client_->DeleteObject(request);
    if (!outcome.IsSuccess()) {
        std::cerr << "S3 DeleteObject error: " << outcome.GetError().GetMessage() << std::endl;
    }
    return outcome.IsSuccess();
}

} // namespace kvcache
