#include <string>
#include <functional>
#include "gen-cpp/blob_rpc.h"
#include "include.h"

namespace BlockStore {
    void conn_init(const std::string& hostname, const int port);
    Errno::type read(const int64_t addr, std::string& value, int init_leader, int retry_time, int sleep_time);
    Errno::type write(const int64_t addr, std::string& write, int init_leader, int retry_time, int sleep_time);
    void compare_logs(int init_leader, int retry_time, int sleep_time);
    void compare_blocks(const int64_t address, int init_leader, int retry_time, int sleep_time);
}
