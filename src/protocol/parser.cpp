/**
 * VaultDB — Protocol Parser Implementation
 *
 * Handles parsing of text commands and dispatching to the LSM engine.
 * The protocol is intentionally simple (one command per line, space-separated)
 * to keep the parser lightweight and easy to debug.
 */

#include "parser.h"

namespace vaultdb {

Command Parser::parse(const std::string& input) {
    Command cmd;
    std::istringstream stream(input);
    std::string token;

    if (stream >> token) {
        // Convert command name to uppercase for case-insensitive matching
        for (auto& c : token) c = std::toupper(c);
        cmd.name = token;
    }

    // Rest of tokens are arguments
    while (stream >> token) {
        cmd.args.push_back(token);
    }

    return cmd;
}

std::string Parser::execute(const Command& cmd, LSMEngine& engine) {
    if (cmd.name == "PING") {
        return "PONG\n";
    }

    if (cmd.name == "SET") {
        if (cmd.args.size() < 2) {
            return "ERROR Usage: SET key value\n";
        }
        // Join all args after the first as the value (allows spaces in values)
        std::string value;
        for (size_t i = 1; i < cmd.args.size(); i++) {
            if (i > 1) value += " ";
            value += cmd.args[i];
        }
        engine.set(cmd.args[0], value);
        return "OK\n";
    }

    if (cmd.name == "GET") {
        if (cmd.args.empty()) {
            return "ERROR Usage: GET key\n";
        }
        auto result = engine.get(cmd.args[0]);
        if (result.has_value()) {
            return "VALUE " + result.value() + "\n";
        }
        return "NULL\n";
    }

    if (cmd.name == "DEL") {
        if (cmd.args.empty()) {
            return "ERROR Usage: DEL key\n";
        }
        engine.del(cmd.args[0]);
        return "OK\n";
    }

    if (cmd.name == "TTL") {
        if (cmd.args.size() < 3) {
            return "ERROR Usage: TTL key seconds value\n";
        }
        // Store value with expiry timestamp encoded in a prefix
        // Format: "EXPIRY:<expiry_timestamp_ms>:<actual_value>"
        auto now = std::chrono::system_clock::now();
        auto expiry = now + std::chrono::seconds(std::stoi(cmd.args[1]));
        auto expiry_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            expiry.time_since_epoch()).count();

        std::string value;
        for (size_t i = 2; i < cmd.args.size(); i++) {
            if (i > 2) value += " ";
            value += cmd.args[i];
        }
        std::string stored = "EXPIRY:" + std::to_string(expiry_ms) + ":" + value;
        engine.set(cmd.args[0], stored);
        return "OK\n";
    }

    if (cmd.name == "BENCH") {
        int n = 10000;
        if (!cmd.args.empty()) {
            try { n = std::stoi(cmd.args[0]); }
            catch (...) { return "ERROR Invalid number\n"; }
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < n; i++) {
            engine.set("bench_key_" + std::to_string(i),
                       "bench_value_" + std::to_string(i));
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();
        double ops_per_sec = (duration > 0)
            ? static_cast<double>(n) / duration * 1000.0
            : 0;

        return "OK " + std::to_string(n) + " ops in " +
               std::to_string(duration) + "ms (" +
               std::to_string(static_cast<int>(ops_per_sec)) + " ops/sec)\n";
    }

    if (cmd.name == "STATS") {
        auto stats = engine.get_stats();
        std::ostringstream oss;
        oss << "STATS"
            << " writes=" << stats.write_count
            << " reads=" << stats.read_count
            << " cache_hits=" << stats.cache_hits
            << " cache_size=" << stats.cache_size
            << " memtable_bytes=" << stats.memtable_size_bytes
            << " memtable_entries=" << stats.memtable_entries
            << " sstables=" << stats.sstable_count
            << " cache_hit_rate=" << static_cast<int>(stats.cache_hit_rate) << "%"
            << " bloom_saved=" << stats.bloom_filter_saved_reads
            << "\n";
        return oss.str();
    }

    return "ERROR Unknown command: " + cmd.name + "\n";
}

}  // namespace vaultdb
