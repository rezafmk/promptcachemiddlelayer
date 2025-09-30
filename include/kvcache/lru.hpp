#pragma once

#include <string>
#include <list>
#include <unordered_map>
#include <optional>

namespace kvcache {

/**
 * @class LRUTracker
 * @brief Manages the order of keys based on a Least Recently Used (LRU) policy.
 *
 * This class is not thread-safe by itself. External synchronization (e.g., a std::mutex)
 * is required if it's accessed from multiple threads.
 *
 * The front of the list is the Most Recently Used (MRU) item, and the back
 * is the Least Recently Used (LRU) item.
 */
class LRUTracker {
public:
    LRUTracker() = default;

    /**
     * @brief Marks a key as recently used by moving it to the front of the list.
     * If the key does not exist, it is inserted.
     * @param key The key to touch.
     */
    void Touch(const std::string& key);

    /**
     * @brief Removes a specific key from the tracker.
     * @param key The key to remove.
     */
    void Remove(const std::string& key);

    /**
     * @brief Evicts the least recently used key and returns it.
     * @return The evicted key, or std::nullopt if the tracker is empty.
     */
    std::optional<std::string> Evict();

    /**
     * @brief Checks if the tracker is empty.
     * @return True if the tracker contains no keys, false otherwise.
     */
    bool IsEmpty() const;

    /**
     * @brief Returns the number of keys in the tracker.
     * @return The total number of keys.
     */
    size_t Size() const;

private:
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, std::list<std::string>::iterator> key_map_;
};

} // namespace kvcache
