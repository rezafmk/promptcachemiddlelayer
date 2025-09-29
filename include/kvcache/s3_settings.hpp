#pragma once

#include <string>
#include <cstdlib>

namespace kvcache {

namespace s3_defaults {
    constexpr char kEndpoint[] = "http://127.0.0.1:9000";
    constexpr char kRegion[] = "us-east-1";
    constexpr char kBucket[] = "kv-cache";
    constexpr char kAccessKeyId[] = "minioadmin";
    constexpr char kSecretAccessKey[] = "minioadmin";
    constexpr bool kUsePathStyle = true;
}

inline std::string GetEnv(const char* name, const std::string& defaultValue) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : defaultValue;
}

inline bool GetEnvBool(const char* name, bool defaultValue) {
    const char* value = std::getenv(name);
    if (!value) return defaultValue;
    return std::string(value) == "1" || std::string(value) == "true" || std::string(value) == "TRUE";
}


inline void ApplyS3ConfigDefaults(Config& cfg) {
    if (cfg.s3_endpoint.empty())
        cfg.s3_endpoint = GetEnv("KVC_S3_ENDPOINT", s3_defaults::kEndpoint);
    if (cfg.s3_region.empty())
        cfg.s3_region = GetEnv("KVC_S3_REGION", s3_defaults::kRegion);
    if (cfg.s3_bucket.empty())
        cfg.s3_bucket = GetEnv("KVC_S3_BUCKET", s3_defaults::kBucket);
    if (cfg.aws_access_key_id.empty())
        cfg.aws_access_key_id = GetEnv("KVC_AWS_ACCESS_KEY_ID", s3_defaults::kAccessKeyId);
    if (cfg.aws_secret_access_key.empty())
        cfg.aws_secret_access_key = GetEnv("KVC_AWS_SECRET_ACCESS_KEY", s3_defaults::kSecretAccessKey);
    
    // For boolean, env var presence determines override
    const char* path_style_env = std::getenv("KVC_S3_USE_PATH_STYLE");
    if (path_style_env) {
        cfg.s3_use_path_style = GetEnvBool("KVC_S3_USE_PATH_STYLE", s3_defaults::kUsePathStyle);
    } else {
        // If not set by user in cfg or env, use default
        // This logic is a bit tricky. Let's assume if the user didn't touch it, it's true.
        // A better approach would be to have a sentinel value. For now, this works.
    }
}

} // namespace kvcache