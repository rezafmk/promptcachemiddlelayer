#include "kvcache/hash.hpp"
#include "xxh3.h"

#include <vector>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <cstring>

namespace kvcache {

// Helper to append little-endian data to a vector
template <typename T>
void append_le(std::vector<std::uint8_t>& buf, T value) {
    for (size_t i = 0; i < sizeof(T); ++i) {
        buf.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

PrefixKey MakePrefixKey(const std::vector<std::uint32_t>& tokens,
                        std::uint32_t block_size,
                        const std::string& model_id) {
    std::vector<std::uint8_t> serialization_buffer;
    serialization_buffer.reserve(1 + sizeof(block_size) + sizeof(uint16_t) + model_id.length() + tokens.size() * sizeof(uint32_t));

    // 1. Version
    serialization_buffer.push_back(1);

    // 2. Block size
    append_le(serialization_buffer, block_size);

    // 3. Model ID
    if (model_id.length() > UINT16_MAX) {
        throw std::runtime_error("Model ID is too long.");
    }
    append_le(serialization_buffer, static_cast<std::uint16_t>(model_id.length()));
    serialization_buffer.insert(serialization_buffer.end(), model_id.begin(), model_id.end());

    // 4. Tokens
    for (const auto& token : tokens) {
        append_le(serialization_buffer, token);
    }

    // Compute XXH3-128 hash
    XXH128_hash_t hash = XXH3_128bits(serialization_buffer.data(), serialization_buffer.size());

    // Return as a 16-byte array
    PrefixKey key;
    std::memcpy(key.data(), &hash, sizeof(hash));
    return key;
}

std::string ToHex(const PrefixKey& key) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (const auto& byte : key) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

} // namespace kvcache
