#include "kvcache/s3_client.hpp"
#include "kvcache/s3_settings.hpp"

#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/DeleteObjectRequest.h>
#include <iostream>
#include <sstream>

namespace kvcache {

// PIMPL for hiding AWS SDK headers
struct S3Client::S3ClientImpl {
    Aws::SDKOptions aws_options;
    std::unique_ptr<Aws::S3::S3Client> s3;
    std::string bucket;
};

S3Client::S3Client(const Config& cfg) : p_impl(std::make_unique<S3ClientImpl>()) {
    p_impl->aws_options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Fatal;
    Aws::InitAPI(p_impl->aws_options);
    
    Aws::Client::ClientConfiguration aws_cfg;
    if (!cfg.s3_region.empty()) {
        aws_cfg.region = cfg.s3_region;
    }
    if (!cfg.s3_endpoint.empty()) {
        aws_cfg.endpointOverride = cfg.s3_endpoint;
    }
    
    Aws::Auth::AWSCredentials creds;
    if (!cfg.aws_access_key_id.empty() && !cfg.aws_secret_access_key.empty()) {
        creds.SetAWSAccessKeyId(cfg.aws_access_key_id.c_str());
        creds.SetAWSSecretKey(cfg.aws_secret_access_key.c_str());
    }

    // The AWS C++ SDK uses 'useVirtualAddressing'. Path style is the inverse.
    bool useVirtualAddressing = !cfg.s3_use_path_style;

    p_impl->s3 = std::make_unique<Aws::S3::S3Client>(creds, aws_cfg,
        Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never,
        useVirtualAddressing);
    
    p_impl->bucket = cfg.s3_bucket;
}

S3Client::~S3Client() {
    Aws::ShutdownAPI(p_impl->aws_options);
}

bool S3Client::GetObject(const std::string& key, std::vector<std::uint8_t>* data) {
    Aws::S3::Model::GetObjectRequest request;
    request.SetBucket(p_impl->bucket);
    request.SetKey(key);

    auto outcome = p_impl->s3->GetObject(request);
    if (!outcome.IsSuccess()) {
        // std::cerr << "GetObject error: " << outcome.GetError().GetMessage() << std::endl;
        return false;
    }

    auto& body = outcome.GetResult().GetBody();
    std::stringstream ss;
    ss << body.rdbuf();
    std::string s = ss.str();
    data->assign(s.begin(), s.end());
    return true;
}

bool S3Client::PutObject(const std::string& key, bytes_view data) {
    Aws::S3::Model::PutObjectRequest request;
    request.SetBucket(p_impl->bucket);
    request.SetKey(key);

    auto stream = std::make_shared<Aws::StringStream>();
    stream->write(reinterpret_cast<const char*>(data.data()), data.size());
    request.SetBody(stream);

    auto outcome = p_impl->s3->PutObject(request);
    if (!outcome.IsSuccess()) {
        // std::cerr << "PutObject error: " << outcome.GetError().GetMessage() << std::endl;
        return false;
    }
    return true;
}

bool S3Client::DeleteObject(const std::string& key) {
    Aws::S3::Model::DeleteObjectRequest request;
    request.SetBucket(p_impl->bucket);
    request.SetKey(key);

    auto outcome = p_impl->s3->DeleteObject(request);
    if (!outcome.IsSuccess()) {
        // std::cerr << "DeleteObject error: " << outcome.GetError().GetMessage() << std::endl;
        return false;
    }
    return true;
}

} // namespace kvcache