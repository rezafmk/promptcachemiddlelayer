#pragma once

#include "types.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include "span_compat.hpp"

namespace kvcache {

// Forward declaration of internal state
class KVCacheImpl;

class KVCache {
public:
    explicit KVCache(const Config& cfg);
    ~KVCache();

    // Compute best available cached prefix for 'tokens'.
    LookupResult Lookup(const std::vector<std::uint32_t>& tokens) const;

    // Load the full bytes of one block.
    bool Load(const BlockRef& ref, std::vector<std::uint8_t>* out_bytes);

    // Store one block for the prefix ending at block_index.
    bool Store(const std::vector<std::uint32_t>& tokens,
               std::uint32_t block_index,
               bytes_view block_bytes);

    // Introspection
    std::uint64_t UsedBytes() const;
    std::uint64_t CapacityBytes() const;
    void SetCapacityBytes(std::uint64_t cap);

private:
    // PIMPL Idiom
    std::unique_ptr<KVCacheImpl> p_impl;

    // Disable copy/move
    KVCache(const KVCache&) = delete;
    KVCache& operator=(const KVCache&) = delete;
    KVCache(KVCache&&) = delete;
    KVCache& operator=(KVCache&&) = delete;
};

} // namespace kvcache
