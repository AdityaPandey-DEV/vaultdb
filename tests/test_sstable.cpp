/**
 * VaultDB — SSTable Tests
 *
 * Tests for on-disk Sorted String Table operations:
 *   - Flush from MemTable entries and look up keys
 *   - Compaction merges two SSTables, newer values win
 */
#include <gtest/gtest.h>
#include <filesystem>
#include "engine/sstable.h"
#include "engine/memtable.h"

class SSTableTest : public ::testing::Test {
protected:
    std::string test_dir = "./test_data_sstable";

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(SSTableTest, FlushAndLookup) {
    // Sorted entries (as if from MemTable's std::map)
    std::vector<std::pair<std::string, std::string>> entries = {
        {"apple", "1"},
        {"banana", "2"},
        {"cherry", "3"},
        {"date", "4"},
        {"elderberry", "5"},
    };

    vaultdb::SSTable sstable(test_dir + "/test.sst");
    sstable.flush(entries);

    // Verify all keys are retrievable
    EXPECT_EQ(sstable.get("apple").value(), "1");
    EXPECT_EQ(sstable.get("cherry").value(), "3");
    EXPECT_EQ(sstable.get("elderberry").value(), "5");

    // Non-existent key should return nullopt
    EXPECT_FALSE(sstable.get("fig").has_value());
    EXPECT_FALSE(sstable.get("aaa").has_value());

    EXPECT_EQ(sstable.entry_count(), 5u);
}

TEST_F(SSTableTest, Compaction) {
    // Older SSTable
    std::vector<std::pair<std::string, std::string>> old_entries = {
        {"a", "old_a"},
        {"b", "old_b"},
        {"c", "old_c"},
    };
    vaultdb::SSTable older(test_dir + "/older.sst");
    older.flush(old_entries);

    // Newer SSTable (overlapping key "b")
    std::vector<std::pair<std::string, std::string>> new_entries = {
        {"b", "new_b"},
        {"d", "new_d"},
    };
    vaultdb::SSTable newer(test_dir + "/newer.sst");
    newer.flush(new_entries);

    // Compact: merge both, newer value wins on conflict
    auto merged = vaultdb::SSTable::compact(
        older, newer, test_dir + "/merged.sst");

    EXPECT_EQ(merged->get("a").value(), "old_a");
    EXPECT_EQ(merged->get("b").value(), "new_b");  // newer wins
    EXPECT_EQ(merged->get("c").value(), "old_c");
    EXPECT_EQ(merged->get("d").value(), "new_d");
    EXPECT_EQ(merged->entry_count(), 4u);
}

TEST_F(SSTableTest, PersistenceAcrossReload) {
    // Flush data to disk
    std::string path = test_dir + "/persist.sst";
    {
        vaultdb::SSTable sstable(path);
        std::vector<std::pair<std::string, std::string>> entries = {
            {"key1", "value1"},
            {"key2", "value2"},
            {"key3", "value3"},
        };
        sstable.flush(entries);
    }

    // Create a new SSTable instance from the same file — should rebuild index
    vaultdb::SSTable reloaded(path);
    EXPECT_EQ(reloaded.get("key1").value(), "value1");
    EXPECT_EQ(reloaded.get("key2").value(), "value2");
    EXPECT_EQ(reloaded.get("key3").value(), "value3");
    EXPECT_EQ(reloaded.entry_count(), 3u);
}
