#include "kvcache/api.hpp"
#include "kvcache/hash.hpp"
#include "kvcache/s3_client.hpp"
#include "kvcache/s3_settings.hpp"

#include <iostream>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <chrono>

namespace kvcache {

// --- Internal Data Structures ---

struct BlockMeta {
    std::uint64_t size;
    std::list<std::string>::iterator lru_it;
};

// PIMPL: Private Implementation
class KVCacheImpl {
public:
    KVCacheImpl(const Config& cfg);
    ~KVCacheImpl();

    void GcThreadLoop();

    LookupResult Lookup(const std::vector<std::uint32_t>& tokens) const;
    bool Load(const BlockRef& ref, std::vector<std::uint8_t>* out_bytes);
    bool Store(const std::vector<std::uint32_t>& tokens,
               std::uint32_t block_index,
               bytes_view block_bytes);
    
    std::uint64_t UsedBytes() const;
    std::uint64_t CapacityBytes() const;
    void SetCapacityBytes(std::uint64_t cap);

private:
    std::string make_s3_key(const std::string& prefix_hex, std::uint32_t block_index) const;
    void touch_lru(const std::string& s3_key);
    void evict_lru();

    Config config_;
    std::unique_ptr<S3Client> s3_client_;

    // Thread-safe state
    mutable std::mutex mutex_;
    std::condition_variable cv_gc_;
    std::uint64_t used_bytes_ = 0;
    std::uint64_t capacity_bytes_;

    // In-memory index
    std::unordered_map<std::string, std::uint32_t> prefix_hwm_; // prefix_hex -> highest_contiguous_block_index
    std::unordered_map<std::string, BlockMeta> block_metadata_; // s3_key -> metadata
    std::list<std::string> lru_list_; // MRU at front, LRU at back

    // GC thread
    std::thread gc_thread_;
    bool stop_gc_ = false;
};


// --- KVCacheImpl Implementation ---

KVCacheImpl::KVCacheImpl(const Config& cfg) : config_(cfg) {
    ApplyS3ConfigDefaults(config_);
    capacity_bytes_ = config_.capacity_bytes;
    s3_client_ = std::make_unique<S3Client>(config_);

    // Start GC thread
    gc_thread_ = std::thread(&KVCacheImpl::GcThreadLoop, this);
}

KVCacheImpl::~KVCacheImpl() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_gc_ = true;
    }
    cv_gc_.notify_one();
    if (gc_thread_.joinable()) {
        gc_thread_.join();
    }
}

void KVCacheImpl::GcThreadLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_gc_.wait_for(lock, std::chrono::seconds(1), [this] {
            return stop_gc_ || used_bytes_ > capacity_bytes_;
        });

        if (stop_gc_) {
            break;
        }

        while (used_bytes_ > capacity_bytes_) {
            evict_lru();
        }
    }
}

void KVCacheImpl::evict_lru() {
    // Assumes lock is held
    if (lru_list_.empty()) {
        return;
    }

    std::string key_to_evict = lru_list_.back();
    lru_list_.pop_back();

    auto meta_it = block_metadata_.find(key_to_evict);
    if (meta_it == block_metadata_.end()) {
        return; // Should not happen
    }

    std::uint64_t size_to_free = meta_it->second.size;
    block_metadata_.erase(meta_it);
    
    used_bytes_ -= size_to_free;

    // This is a simplified HWM update. A correct implementation would need to parse the key
    // to find the prefix and potentially rescan, which is complex. For now, we accept
    // that HWMs might not be perfectly accurate after eviction of non-HWM blocks.
    // A full implementation would require a reverse index from block to prefix.
    // For simplicity, we are not adjusting HWM here, which means lookups might fail for
    // prefixes where a middle block was evicted.

    // Unlock to perform S3 operation
    mutex_.unlock();
    s3_client_->DeleteObject(key_to_evict);
    mutex_.lock();
}

std::string KVCacheImpl::make_s3_key(const std::string& prefix_hex, std::uint32_t block_index) const {
    return config_.model_id + "/b" + std::to_string(config_.block_size_tokens) +
           "/" + prefix_hex + "/" + std::to_string(block_index) + ".kv";
}

void KVCacheImpl::touch_lru(const std::string& s3_key) {
    // Assumes lock is held
    auto meta_it = block_metadata_.find(s3_key);
    if (meta_it != block_metadata_.end()) {
        lru_list_.splice(lru_list_.begin(), lru_list_, meta_it->second.lru_it);
    }
}

LookupResult KVCacheImpl::Lookup(const std::vector<std::uint32_t>& tokens) const {
    const std::uint32_t B = config_.block_size_tokens;
    std::uint32_t N = tokens.size();
    std::uint32_t K = (N / B) * B;

    if (K == 0) {
        return {0, {}};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (std::uint32_t k = K; k > 0; k -= B) {
        std::vector<std::uint32_t> prefix_tokens(tokens.begin(), tokens.begin() + k);
        PrefixKey pkey = MakePrefixKey(prefix_tokens, B, config_.model_id);
        std::string pkey_hex = ToHex(pkey);

        auto hwm_it = prefix_hwm_.find(pkey_hex);
        if (hwm_it != prefix_hwm_.end()) {
            std::uint32_t hwm = hwm_it->second;
            std::uint32_t matched_blocks = hwm + 1;
            std::uint32_t matched_tokens = std::min(k, matched_blocks * B);

            LookupResult result;
            result.matched_tokens = matched_tokens;
            
            for (std::uint32_t i = 0; i < matched_tokens / B; ++i) {
                std::string s3_key = make_s3_key(pkey_hex, i);
                auto meta_it = block_metadata_.find(s3_key);
                if (meta_it != block_metadata_.end()) {
                    result.handles.push_back({s3_key, meta_it->second.size, i});
                } else {
                    // This indicates an inconsistency, maybe a block was evicted.
                    // We should return what we have contiguously from block 0.
                    result.matched_tokens = i * B;
                    return result;
                }
            }
            return result;
        }
    }

    return {0, {}};
}

bool KVCacheImpl::Load(const BlockRef& ref, std::vector<std::uint8_t>* out_bytes) {
    if (!s3_client_->GetObject(ref.s3_key, out_bytes)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    touch_lru(ref.s3_key);
    return true;
}

bool KVCacheImpl::Store(const std::vector<std::uint32_t>& tokens,
                        std::uint32_t block_index,
                        bytes_view block_bytes) {
    const std::uint32_t B = config_.block_size_tokens;
    std::uint32_t prefix_token_count = (block_index + 1) * B;

    if (tokens.size() < prefix_token_count) {
        return false;
    }

    std::vector<std::uint32_t> prefix_tokens(tokens.begin(), tokens.begin() + prefix_token_count);
    PrefixKey pkey = MakePrefixKey(prefix_tokens, B, config_.model_id);
    std::string pkey_hex = ToHex(pkey);

    std::string s3_key = make_s3_key(pkey_hex, block_index);

    if (!s3_client_->PutObject(s3_key, block_bytes)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update or insert metadata
    auto meta_it = block_metadata_.find(s3_key);
    if (meta_it == block_metadata_.end()) {
        lru_list_.push_front(s3_key);
        block_metadata_[s3_key] = {block_bytes.size(), lru_list_.begin()};
        used_bytes_ += block_bytes.size();
    } else {
        used_bytes_ -= meta_it->second.size;
        used_bytes_ += block_bytes.size();
        meta_it->second.size = block_bytes.size();
        touch_lru(s3_key);
    }

    // Update HWM if this block extends it
    auto hwm_it = prefix_hwm_.find(pkey_hex);
    if (hwm_it == prefix_hwm_.end()) {
        if (block_index == 0) {
            prefix_hwm_[pkey_hex] = 0;
        }
    } else {
        if (block_index == hwm_it->second + 1) {
            hwm_it->second = block_index;
        }
    }
    
    // Signal GC if over capacity
    if (used_bytes_ > capacity_bytes_) {
        cv_gc_.notify_one();
    }

    return true;
}

std::uint64_t KVCacheImpl::UsedBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return used_bytes_;
}

std::uint64_t KVCacheImpl::CapacityBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacity_bytes_;
}

void KVCacheImpl::SetCapacityBytes(std::uint64_t cap) {
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_bytes_ = cap;
    if (used_bytes_ > capacity_bytes_) {
        cv_gc_.notify_one();
    }
}


// --- KVCache Public API (forwarding to PIMPL) ---

KVCache::KVCache(const Config& cfg) : p_impl(std::make_unique<KVCacheImpl>(cfg)) {}
KVCache::~KVCache() = default;
LookupResult KVCache::Lookup(const std::vector<std::uint32_t>& tokens) const { return p_impl->Lookup(tokens); }
bool KVCache::Load(const BlockRef& ref, std::vector<std::uint8_t>* out_bytes) { return p_impl->Load(ref, out_bytes); }
bool KVCache::Store(const std::vector<std::uint32_t>& tokens, std::uint32_t block_index, bytes_view block_bytes) {
    return p_impl->Store(tokens, block_index, block_bytes);
}
std::uint64_t KVCache::UsedBytes() const { return p_impl->UsedBytes(); }
std::uint64_t KVCache::CapacityBytes() const { return p_impl->CapacityBytes(); }
void KVCache::SetCapacityBytes(std::uint64_t cap) { p_impl->SetCapacityBytes(cap); }

} // namespace kvcache
