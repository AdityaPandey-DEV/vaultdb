/**
 * VaultDB — MemTable Tests
 */
#include <gtest/gtest.h>
#include "engine/memtable.h"

TEST(MemTableTest, SetGetDel) {
    vaultdb::MemTable mt(1024 * 1024);

    mt.set("name", "VaultDB");
    mt.set("version", "1.0");

    EXPECT_EQ(mt.get("name").value(), "VaultDB");
    EXPECT_EQ(mt.get("version").value(), "1.0");
    EXPECT_FALSE(mt.get("nonexistent").has_value());

    mt.del("name");
    auto result = mt.get("name");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), vaultdb::TOMBSTONE);
}

TEST(MemTableTest, SizeTracking) {
    vaultdb::MemTable mt(1024 * 1024);

    EXPECT_EQ(mt.size_bytes(), 0u);

    mt.set("key", "value");
    EXPECT_EQ(mt.size_bytes(), 8u);  // "key"(3) + "value"(5) = 8

    mt.set("key", "new");
    EXPECT_EQ(mt.size_bytes(), 6u);  // "key"(3) + "new"(3) = 6

    EXPECT_EQ(mt.count(), 1u);
}

TEST(MemTableTest, SortedEntries) {
    vaultdb::MemTable mt(1024 * 1024);

    mt.set("cherry", "3");
    mt.set("apple", "1");
    mt.set("banana", "2");

    auto entries = mt.get_sorted_entries();

    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].first, "apple");
    EXPECT_EQ(entries[1].first, "banana");
    EXPECT_EQ(entries[2].first, "cherry");
}
