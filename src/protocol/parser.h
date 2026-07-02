#pragma once
/**
 * VaultDB — Protocol Parser
 *
 * Parses simple text commands (similar to Redis RESP but simpler).
 *
 * Commands:
 *   SET key value\n       → store key-value pair
 *   GET key\n             → retrieve value
 *   DEL key\n             → delete key
 *   TTL key seconds\n     → set key with expiry
 *   PING\n                → health check → returns PONG
 *   BENCH n\n             → run n SET operations, return ops/sec
 *
 * Responses:
 *   OK\n                  → success
 *   VALUE <data>\n        → get result
 *   NULL\n                → key not found
 *   ERROR <message>\n     → error
 */

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

#include "../engine/lsm_engine.h"

namespace vaultdb {

struct Command {
    std::string name;  // SET, GET, DEL, TTL, PING, BENCH
    std::vector<std::string> args;
};

class Parser {
public:
    /**
     * Parse a raw command string into a Command struct.
     * @param input Raw string like "SET mykey myvalue\n"
     * @return Parsed Command with name and args.
     */
    static Command parse(const std::string& input);

    /**
     * Execute a parsed command against the LSM engine.
     * @param cmd The parsed command.
     * @param engine Reference to the LSM engine.
     * @return Response string to send back to the client.
     */
    static std::string execute(const Command& cmd, LSMEngine& engine);
};

}  // namespace vaultdb
