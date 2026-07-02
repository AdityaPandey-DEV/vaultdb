/**
 * VaultDB — Bloom Filter Implementation
 *
 * Hash functions are based on FNV-1a (Fowler-Noll-Vo) with different
 * seeds to produce independent bit positions. FNV-1a is chosen because:
 *   - Extremely fast (just XOR + multiply per byte)
 *   - Good distribution (low collision rate)
 *   - No external dependencies (pure C++)
 */

#include "bloom_filter.h"

namespace vaultdb {

BloomFilter::BloomFilter(size_t expected_elements, size_t bits_per_element)
    : num_bits_(expected_elements * bits_per_element),
      bits_(expected_elements * bits_per_element, false) {
    // Ensure at least 64 bits to avoid edge cases
    if (num_bits_ < 64) {
        num_bits_ = 64;
        bits_.resize(64, false);
    }
}

void BloomFilter::add(const std::string& key) {
    bits_[hash1(key) % num_bits_] = true;
    bits_[hash2(key) % num_bits_] = true;
    bits_[hash3(key) % num_bits_] = true;
}

bool BloomFilter::might_contain(const std::string& key) const {
    return bits_[hash1(key) % num_bits_] &&
           bits_[hash2(key) % num_bits_] &&
           bits_[hash3(key) % num_bits_];
}

void BloomFilter::clear() {
    std::fill(bits_.begin(), bits_.end(), false);
}

/**
 * Hash Function 1: FNV-1a with standard offset basis.
 *
 * FNV-1a algorithm:
 *   hash = offset_basis
 *   for each byte in input:
 *     hash = hash XOR byte
 *     hash = hash * FNV_prime
 *
 * The XOR-first variant (FNV-1a) has better avalanche properties
 * than the multiply-first variant (FNV-1).
 */
size_t BloomFilter::hash1(const std::string& key) const {
    uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
    for (char c : key) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;  // FNV prime
    }
    return static_cast<size_t>(hash);
}

/**
 * Hash Function 2: FNV-1a with a different seed.
 *
 * By starting with a different offset basis, the same input produces
 * completely different bit positions, which is essential for the
 * Bloom Filter's false positive guarantees.
 */
size_t BloomFilter::hash2(const std::string& key) const {
    uint64_t hash = 6364136223846793005ULL;  // Different seed
    for (char c : key) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return static_cast<size_t>(hash);
}

/**
 * Hash Function 3: DJB2a (Daniel J. Bernstein variant).
 *
 * A completely different algorithm for maximum independence:
 *   hash = 5381
 *   for each byte: hash = ((hash << 5) + hash) XOR byte
 *
 * Using a different algorithm (not just a different seed) minimizes
 * correlated collisions between the hash functions.
 */
size_t BloomFilter::hash3(const std::string& key) const {
    uint64_t hash = 5381;
    for (char c : key) {
        hash = ((hash << 5) + hash) ^ static_cast<uint64_t>(c);
    }
    return static_cast<size_t>(hash);
}

}  // namespace vaultdb
