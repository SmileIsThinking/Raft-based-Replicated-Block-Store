#include <string>

#define STORE "BLOCK_STORE"
#define BLOCK_SIZE 0x1000

#define LOG "LOG"
#define STATE "STATE"

namespace ServerStore {

    int init(int node_id);
    int read(const int64_t addr, std::string& value);
    int write(const int64_t addr, const std::string& value, int64_t& seq);
    int full_read(std::string& content);
    int full_write(std::string& content);

    int append_log();
    int read_log();
    
    int update_state(int currentTerm, int votedFor);
    int read_state();
}

