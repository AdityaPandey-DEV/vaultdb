#pragma once
/**
 * VaultDB — Bloom Filter
 *
 * A probabilistic data structure that can tell you with 100% certainty
 * if a key is NOT in a set, but only with high probability if it IS.
 *
 * Used in SSTables to skip unnecessary disk reads:
 *   - If bloom.might_contain("key") returns false → key is DEFINITELY not
 *     in this SSTable, so we skip the disk I/O entirely.
 *   - If it returns true → key MIGHT be in the SSTable, so we do the
 *     actual disk lookup.
 *
 * False positive rate with 3 hash functions and 10 bits per key:
 *   FPR ≈ (1 - e^(-k*n/m))^k ≈ 1.7%
 *
 * Trade-off: ~1.25 bytes of RAM per key stored to save expensive disk reads.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vaultdb {

class BloomFilter {
public:
    /**
     * Create a Bloom Filter optimized for the expected number of elements.
     *
     * @param expected_elements Number of keys we expect to insert.
     * @param bits_per_element Bits allocated per element (default 10).
     *        Higher values reduce false positive rate at the cost of memory.
     *
     * Memory usage: expected_elements * bits_per_element / 8 bytes
     * Example: 10,000 keys × 10 bits = 12.5 KB of RAM
     */
    explicit BloomFilter(size_t expected_elements = 1000,
                         size_t bits_per_element = 10);

    /**
     * Add a key to the filter.
     * This sets k=3 bits in the bit array using 3 different hash functions.
     *
     * @param key The key to insert.
     *
     * Time: O(k) = O(3) = O(1)
     */
    void add(const std::string& key);

    /**
     * Check if a key MIGHT be in the filter.
     *
     * @param key The key to check.
     * @return false → key is DEFINITELY not present (100% certain)
     *         true  → key MIGHT be present (small chance of false positive)
     *
     * Time: O(k) = O(3) = O(1)
     */
    bool might_contain(const std::string& key) const;

    /**
     * Reset the filter (clear all bits).
     */
    void clear();

    /** Number of bits in the filter. */
    size_t bit_count() const { return num_bits_; }

    /** Memory usage in bytes. */
    size_t memory_bytes() const { return (num_bits_ + 7) / 8; }

private:
    /**
     * Three independent hash functions based on FNV-1a variants.
     * Each produces a different hash by using a different seed/offset.
     *
     * Why 3 hash functions?
     *   - 1 hash → too many false positives
     *   - 3 hashes → optimal balance for 10 bits/element
     *   - More hashes → diminishing returns and slower inserts
     */
    size_t hash1(const std::string& key) const;
    size_t hash2(const std::string& key) const;
    size_t hash3(const std::string& key) const;

    std::vector<bool> bits_;
    size_t num_bits_;
};

}  // namespace vaultdb
