#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kvcache {

// A simplified, C++17-compatible view over a contiguous sequence of bytes,
// similar to std::span<const std::uint8_t>.
class bytes_view {
public:
    // Default constructor
    bytes_view() : data_(nullptr), size_(0) {}

    // Constructor from pointer and size
    bytes_view(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    // Constructor from a vector of bytes
    bytes_view(const std::vector<std::uint8_t>& vec) : data_(vec.data()), size_(vec.size()) {}

    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    const std::uint8_t* begin() const { return data_; }
    const std::uint8_t* end() const { return data_ + size_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

} // namespace kvcache
