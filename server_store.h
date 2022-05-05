#include <string>
#include "gen-cpp/raft_rpc.h"
#define STORE "BLOCK_STORE"
#define BLOCK_SIZE 0x1000

#define LOG "LOG"
#define LOG_NUM "LOG_NUM"
#define STATE "STATE"

// const int entrySize = 4096;
namespace ServerStore {

    int init(int node_id);
    int read(const int64_t addr, std::string& value);
    int write(const int64_t addr, const std::string& value, int64_t& seq);
    int full_read(std::string& content);
    int full_write(std::string& content);

    int append_log(entry& logEntry);
    int read_log(int index, entry& logEntry);
    int remove_log(int index);
    int write_state(int currentTerm, int votedFor);
    int read_state(int* currentTerm, int* votedFor);
}

/*

struct entry {
    // 0: read, 1: write
    1: i32 commmand,
    2: i32 term,   
    3: i64 address,
    4: string content,
}

*/