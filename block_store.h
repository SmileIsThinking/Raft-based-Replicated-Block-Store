#include <string>
#include <functional>
#include "gen-cpp/blob_rpc.h"
#include "include.h"

namespace BlockStore {
    void conn_init(const std::string& hostname, const int port);
    Errno::type read(const int64_t addr, std::string& value, int retry_time, int sleep_time);
    Errno::type write(const int64_t addr, std::string& write, int retry_time, int sleep_time);
}
