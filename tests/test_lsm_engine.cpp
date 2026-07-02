/**
 * VaultDB — LSM Engine Integration Tests
 */
#include <gtest/gtest.h>
#include <filesystem>
#include "engine/lsm_engine.h"

class LSMEngineTest : public ::testing::Test {
protected:
    std::string test_dir = "./test_data_lsm";

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(LSMEngineTest, SetAndGet) {
    vaultdb::LSMEngine engine(test_dir, 100, 1024 * 1024, 9999);

    engine.set("name", "VaultDB");
    engine.set("version", "1.0");

    EXPECT_EQ(engine.get("name").value(), "VaultDB");
    EXPECT_EQ(engine.get("version").value(), "1.0");
    EXPECT_FALSE(engine.get("nonexistent").has_value());
}

TEST_F(LSMEngineTest, DeleteKey) {
    vaultdb::LSMEngine engine(test_dir, 100, 1024 * 1024, 9999);

    engine.set("key", "value");
    EXPECT_TRUE(engine.get("key").has_value());

    engine.del("key");
    EXPECT_FALSE(engine.get("key").has_value());
}

TEST_F(LSMEngineTest, CacheHit) {
    vaultdb::LSMEngine engine(test_dir, 100, 1024 * 1024, 9999);

    engine.set("cached_key", "cached_value");

    // First read — may or may not be from cache
    engine.get("cached_key");
    // Second read — should be a cache hit
    auto result = engine.get("cached_key");

    EXPECT_EQ(result.value(), "cached_value");

    auto stats = engine.get_stats();
    EXPECT_GT(stats.cache_hits, 0u);
}

TEST_F(LSMEngineTest, CrashRecoveryViaWAL) {
    // Phase 1: Write data then "crash" (destroy engine)
    {
        vaultdb::LSMEngine engine(test_dir, 100, 1024 * 1024, 9999);
        engine.set("persist_key", "persist_value");
        engine.set("another_key", "another_value");
        // Engine destroyed here — simulates crash
    }

    // Phase 2: Create new engine from same data_dir — should recover from WAL
    {
        vaultdb::LSMEngine engine(test_dir, 100, 1024 * 1024, 9999);

        auto result = engine.get("persist_key");
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "persist_value");

        auto result2 = engine.get("another_key");
        EXPECT_TRUE(result2.has_value());
        EXPECT_EQ(result2.value(), "another_value");
    }
}

TEST_F(LSMEngineTest, FlushToSSTable) {
    // Small memtable (256 bytes) to force flush
    vaultdb::LSMEngine engine(test_dir, 100, 256, 9999);

    // Write enough data to trigger flush
    for (int i = 0; i < 50; i++) {
        engine.set("key_" + std::to_string(i),
                    "value_" + std::to_string(i));
    }

    // Data should still be accessible after flush
    EXPECT_EQ(engine.get("key_0").value(), "value_0");
    EXPECT_EQ(engine.get("key_49").value(), "value_49");

    auto stats = engine.get_stats();
    EXPECT_GT(stats.sstable_count, 0u);
}
