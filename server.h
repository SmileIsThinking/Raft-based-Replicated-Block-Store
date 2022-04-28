#define HB_FREQ 2

#define BLOB_SERVER_WORKER 5
#define PB_SERVER_WORKER (BLOB_SERVER_WORKER + 1)

#include "gen-cpp/blob_rpc.h"
#include "gen-cpp/pb_rpc.h"

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/async/TConcurrentClientSyncInfo.h>


#include <thread>
#include <iostream>
#include <time.h>

#include "include.h"
#include "server_store.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::concurrency;
using namespace ::apache::thrift::server;

std::string my_addr;
int my_blob_port;
int my_pb_port;

std::atomic<bool> is_primary;
time_t last_heartbeat;
// assuming single backup
std::atomic<int> num_write_requests;
std::atomic<bool> pending_backup;
std::atomic<bool> has_backup;
std::string backup_hostname;
int backup_port;

class blob_rpcHandler : virtual public blob_rpcIf {
public:
    blob_rpcHandler() {
        std::cout << "Blob Server Started" << std::endl;
    }

    void ping() {
        printf("%s: blob_ping\n", is_primary ? "primary" : "backup");
    }

    void read(read_ret& _return, const int64_t addr);
    Errno::type write(const int64_t addr, const std::string& value);
};

class pb_rpcHandler : virtual public pb_rpcIf {
public:
    pb_rpcHandler() {
        std::cout << "PB Server Started" << std::endl;
    }

    void ping() {
        printf("%s: pb_ping\n", is_primary ? "primary" : "backup");
    }

    void new_backup(new_backup_ret& ret, const std::string& hostname, const int32_t port);
    void new_backup_succeed();
    PB_Errno::type update(const int64_t addr, const std::string& value, const std::vector<int64_t>& seq);
    void heartbeat();
};
