#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <array>

namespace kvcache {

using PrefixKey = std::array<std::uint8_t, 16>;

PrefixKey MakePrefixKey(const std::vector<std::uint32_t>& tokens,
                        std::uint32_t block_size,
                        const std::string& model_id);

std::string ToHex(const PrefixKey& key);

} // namespace kvcache