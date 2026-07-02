/**
 * VaultDB — WAL Tests
 */
#include <gtest/gtest.h>
#include <filesystem>
#include "engine/wal.h"

class WALTest : public ::testing::Test {
protected:
    std::string test_path = "./test_data/wal_test.log";

    void SetUp() override {
        std::filesystem::create_directories("./test_data");
        std::filesystem::remove(test_path);
    }

    void TearDown() override {
        std::filesystem::remove_all("./test_data");
    }
};

TEST_F(WALTest, AppendAndRecover) {
    {
        vaultdb::WAL wal(test_path);
        wal.append(vaultdb::WALOp::SET, "key1", "value1");
        wal.append(vaultdb::WALOp::SET, "key2", "value2");
        wal.append(vaultdb::WALOp::DELETE, "key1", "");
    }

    // Simulate crash recovery — new WAL instance reads the file
    vaultdb::WAL wal2(test_path);
    auto entries = wal2.recover();

    ASSERT_EQ(entries.size(), 3u);

    EXPECT_EQ(entries[0].key, "key1");
    EXPECT_EQ(entries[0].value, "value1");
    EXPECT_EQ(entries[0].op, vaultdb::WALOp::SET);

    EXPECT_EQ(entries[1].key, "key2");
    EXPECT_EQ(entries[1].value, "value2");

    EXPECT_EQ(entries[2].key, "key1");
    EXPECT_EQ(entries[2].op, vaultdb::WALOp::DELETE);
}

TEST_F(WALTest, CheckpointClearsFile) {
    vaultdb::WAL wal(test_path);
    wal.append(vaultdb::WALOp::SET, "key1", "value1");
    wal.append(vaultdb::WALOp::SET, "key2", "value2");

    wal.checkpoint();

    auto entries = wal.recover();
    EXPECT_EQ(entries.size(), 0u);
}
