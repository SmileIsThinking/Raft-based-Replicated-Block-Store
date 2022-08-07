#include <string>
#include "gen-cpp/raft_rpc.h"
#define STORE "BLOCK_STORE"
#define BLOCK_SIZE 0x1000

#define LOG "LOG"
#define LOG_NUM "LOG_NUM"
#define STATE "STATE"

namespace ServerStore {

    int init(int node_id);
    int read(const int64_t addr, std::string& value);
    int write(const int64_t addr, const std::string& value);
    int full_read(std::string& content);
    int full_write(std::string& content);

    int append_log(const std::vector<entry>& logEntries);
    int read_log_num();
    entry read_log(int index);
    std::vector<entry> read_full_log();

    int remove_log(int index);
    int write_state(int currentTerm, int votedFor);
    int read_state(int* currentTerm, int* votedFor);
}