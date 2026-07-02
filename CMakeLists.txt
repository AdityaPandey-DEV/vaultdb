cmake_minimum_required(VERSION 3.16)
project(VaultDB VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags for Release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ── Source files ─────────────────────────────────────────────
set(VAULTDB_SOURCES
    src/engine/wal.cpp
    src/engine/memtable.cpp
    src/engine/sstable.cpp
    src/engine/lsm_engine.cpp
    src/protocol/parser.cpp
    src/server/connection.cpp
    src/server/server.cpp
)

# ── Main executable ──────────────────────────────────────────
add_executable(vaultdb src/main.cpp ${VAULTDB_SOURCES})
target_include_directories(vaultdb PRIVATE src)

# ── GoogleTest ───────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Prevent GoogleTest from overriding our compiler/linker options
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ── Test executable ──────────────────────────────────────────
add_executable(vaultdb_tests
    tests/test_lru_cache.cpp
    tests/test_wal.cpp
    tests/test_memtable.cpp
    tests/test_sstable.cpp
    tests/test_lsm_engine.cpp
    ${VAULTDB_SOURCES}
)
target_include_directories(vaultdb_tests PRIVATE src)
target_link_libraries(vaultdb_tests GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(vaultdb_tests)
