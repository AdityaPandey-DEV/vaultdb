/**
 * VaultDB — LRU Cache Tests
 */
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "cache/lru_cache.h"

TEST(LRUCacheTest, EvictionOrder) {
    LRUCache<std::string, std::string> cache(3);

    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");

    // Cache is full. Adding "d" should evict "a" (least recently used)
    cache.put("d", "4");

    EXPECT_FALSE(cache.get("a").has_value());  // evicted
    EXPECT_EQ(cache.get("b").value(), "2");
    EXPECT_EQ(cache.get("c").value(), "3");
    EXPECT_EQ(cache.get("d").value(), "4");
}

TEST(LRUCacheTest, AccessUpdatesOrder) {
    LRUCache<std::string, std::string> cache(3);

    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");

    // Access "a" to make it most recently used
    cache.get("a");

    // Adding "d" should now evict "b" (least recently used after "a" was accessed)
    cache.put("d", "4");

    EXPECT_EQ(cache.get("a").value(), "1");  // still present
    EXPECT_FALSE(cache.get("b").has_value()); // evicted
    EXPECT_EQ(cache.get("c").value(), "3");
    EXPECT_EQ(cache.get("d").value(), "4");
}

TEST(LRUCacheTest, ThreadSafety) {
    LRUCache<int, int> cache(1000);
    const int num_threads = 8;
    const int ops_per_thread = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&cache, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++) {
                int key = t * ops_per_thread + i;
                cache.put(key, key * 10);
                cache.get(key);
            }
        });
    }

    for (auto& thread : threads) thread.join();

    // No crashes, no undefined behavior — that's the main assertion
    EXPECT_LE(cache.size(), 1000u);
}

TEST(LRUCacheTest, UpdateExistingKey) {
    LRUCache<std::string, std::string> cache(3);

    cache.put("key", "old_value");
    cache.put("key", "new_value");

    EXPECT_EQ(cache.get("key").value(), "new_value");
    EXPECT_EQ(cache.size(), 1u);
}

TEST(LRUCacheTest, Remove) {
    LRUCache<std::string, std::string> cache(5);

    cache.put("a", "1");
    cache.put("b", "2");

    EXPECT_TRUE(cache.remove("a"));
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_FALSE(cache.remove("nonexistent"));
    EXPECT_EQ(cache.size(), 1u);
}
