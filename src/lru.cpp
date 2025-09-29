#include "kvcache/lru.hpp"

namespace kvcache {

void LRUTracker::Touch(const std::string& key) {
    auto it = key_map_.find(key);
    if (it != key_map_.end()) {
        // Key exists, move it to the front (MRU position)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    } else {
        // Key doesn't exist, insert it at the front
        lru_list_.push_front(key);
        key_map_[key] = lru_list_.begin();
    }
}

void LRUTracker::Remove(const std::string& key) {
    auto it = key_map_.find(key);
    if (it != key_map_.end()) {
        lru_list_.erase(it->second);
        key_map_.erase(it);
    }
}

std::optional<std::string> LRUTracker::Evict() {
    if (lru_list_.empty()) {
        return std::nullopt;
    }

    // The key to evict is at the back of the list (LRU position)
    std::string evicted_key = lru_list_.back();
    
    // Remove it from the list and the map
    lru_list_.pop_back();
    key_map_.erase(evicted_key);

    return evicted_key;
}

bool LRUTracker::IsEmpty() const {
    return key_map_.empty();
}

size_t LRUTracker::Size() const {
    return key_map_.size();
}

} // namespace kvcache
