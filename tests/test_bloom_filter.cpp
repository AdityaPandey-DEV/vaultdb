/**
 * VaultDB — Bloom Filter Unit Tests
 *
 * Tests the probabilistic guarantees of the Bloom Filter:
 *   1. Zero false negatives (if added, must be found)
 *   2. Low false positive rate (~1-3% with 10 bits/element)
 */

#include <gtest/gtest.h>
#include "engine/bloom_filter.h"
#include <string>

using namespace vaultdb;

// Test: Every inserted key must be found (zero false negatives)
TEST(BloomFilterTest, NoFalseNegatives) {
    BloomFilter bf(1000);

    // Insert 1000 keys
    for (int i = 0; i < 1000; i++) {
        bf.add("key_" + std::to_string(i));
    }

    // Every inserted key MUST return true
    for (int i = 0; i < 1000; i++) {
        EXPECT_TRUE(bf.might_contain("key_" + std::to_string(i)))
            << "False negative for key_" << i;
    }
}

// Test: False positive rate should be reasonable (< 5%)
TEST(BloomFilterTest, FalsePositiveRate) {
    BloomFilter bf(1000);

    // Insert 1000 keys
    for (int i = 0; i < 1000; i++) {
        bf.add("inserted_" + std::to_string(i));
    }

    // Test 10000 keys that were NEVER inserted
    int false_positives = 0;
    int total_checks = 10000;
    for (int i = 0; i < total_checks; i++) {
        if (bf.might_contain("not_inserted_" + std::to_string(i))) {
            false_positives++;
        }
    }

    double fpr = static_cast<double>(false_positives) / total_checks * 100.0;

    // With 10 bits/element and 3 hash functions, FPR should be ~1.7%
    // We allow up to 5% to account for hash variance
    EXPECT_LT(fpr, 5.0)
        << "False positive rate too high: " << fpr << "%"
        << " (" << false_positives << " / " << total_checks << ")";
}

// Test: Empty filter returns false for everything
TEST(BloomFilterTest, EmptyFilter) {
    BloomFilter bf(100);

    EXPECT_FALSE(bf.might_contain("any_key"));
    EXPECT_FALSE(bf.might_contain("another_key"));
    EXPECT_FALSE(bf.might_contain(""));
}

// Test: Clear resets the filter
TEST(BloomFilterTest, ClearResetsFilter) {
    BloomFilter bf(100);

    bf.add("test_key");
    EXPECT_TRUE(bf.might_contain("test_key"));

    bf.clear();
    EXPECT_FALSE(bf.might_contain("test_key"));
}

// Test: Memory usage is reasonable
TEST(BloomFilterTest, MemoryUsage) {
    BloomFilter bf(10000, 10);  // 10000 elements × 10 bits = 100000 bits

    // Should use approximately 12500 bytes (100000 / 8)
    EXPECT_EQ(bf.bit_count(), 100000u);
    EXPECT_EQ(bf.memory_bytes(), 12500u);
}

// Test: Single-element filter works
TEST(BloomFilterTest, SingleElement) {
    BloomFilter bf(1);

    bf.add("only_key");
    EXPECT_TRUE(bf.might_contain("only_key"));
}
