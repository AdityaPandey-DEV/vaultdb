#pragma once
/**
 * VaultDB — LRU Cache (Least Recently Used)
 *
 * WHY doubly-linked list + hashmap gives O(1) for both operations:
 * ================================================================
 * - HashMap (unordered_map): O(1) lookup by key → gives us the node
 * - Doubly-linked list: O(1) move-to-front and O(1) removal from any position
 *   (because we store iterators, not indices — iterator invalidation doesn't
 *    happen on splice/erase of OTHER elements in std::list)
 *
 * WHY we store list iterators in the hashmap (not copies):
 * ========================================================
 * If we stored copies of values, moving to front would require:
 *   1. Find in hashmap: O(1)
 *   2. Search list for the node: O(n) ← BAD
 *   3. Move to front: O(1)
 * By storing iterators, step 2 becomes O(1) because we jump directly
 * to the list node. Total: O(1) + O(1) + O(1) = O(1).
 *
 * Thread-safety: All public methods acquire a mutex lock.
 */

#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

template <typename K, typename V>
class LRUCache {
public:
    /**
     * Construct an LRU cache with the given capacity.
     * When the cache is full, the least recently used entry is evicted.
     */
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    /**
     * Get the value associated with key.
     * Moves the accessed entry to the front (most recently used).
     *
     * @param key The key to look up.
     * @return The value if found, std::nullopt otherwise.
     *
     * Time: O(1) — hashmap lookup + list splice
     */
    std::optional<V> get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;  // Cache miss
        }

        // Move to front (most recently used)
        // splice moves the element without invalidating iterators
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    /**
     * Insert or update a key-value pair.
     * If the cache is full, evicts the least recently used entry.
     *
     * @param key The key to insert.
     * @param value The value to associate with the key.
     *
     * Time: O(1) — hashmap insert + list push_front + optional eviction
     */
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = index_.find(key);
        if (it != index_.end()) {
            // Key exists — update value and move to front
            it->second->second = value;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }

        // Evict LRU entry if at capacity
        if (items_.size() >= capacity_) {
            // The back of the list is the least recently used
            auto& lru = items_.back();
            index_.erase(lru.first);
            items_.pop_back();
        }

        // Insert new entry at front
        items_.emplace_front(key, value);
        index_[key] = items_.begin();
    }

    /**
     * Remove a key from the cache.
     * @return true if the key was found and removed.
     */
    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = index_.find(key);
        if (it == index_.end()) return false;

        items_.erase(it->second);
        index_.erase(it);
        return true;
    }

    /**
     * Check if a key exists in the cache (without affecting LRU order).
     */
    bool contains(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_.find(key) != index_.end();
    }

    /**
     * Get the current number of entries in the cache.
     */
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    /**
     * Clear all entries from the cache.
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
        index_.clear();
    }

private:
    size_t capacity_;
    std::mutex mutex_;

    // Doubly-linked list: front = most recently used, back = least recently used
    std::list<std::pair<K, V>> items_;

    // Hashmap: key → iterator into the list (for O(1) access)
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> index_;
};
