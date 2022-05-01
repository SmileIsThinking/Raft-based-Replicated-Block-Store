#define HB_FREQ 5

#include "gen-cpp/blob_rpc.h"
#include "gen-cpp/pb_rpc.h"
#include "gen-cpp/raft_rpc.h"

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
#include <vector>

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
::std::shared_ptr<pb_rpcIf> other = nullptr;
::std::shared_ptr<::apache::thrift::async::TConcurrentClientSyncInfo> otherSyncInfo = nullptr;

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
    PB_Errno::type update(const int64_t addr, const std::string& value, const int64_t seq);
    void heartbeat();
};

/* ===================================== */
/* Raft Misc Variables */
/* ===================================== */
std::shared_ptr<raft_rpcIf> rpcServer[NODE_NUM] = {nullptr, nullptr, nullptr};
std::shared_ptr<::apache::thrift::async::TConcurrentClientSyncInfo> \
syncInfo[NODE_NUM] = {nullptr, nullptr, nullptr};

int myID;

// 0: Leader
// 1: Candidate
// 2: Follower
std::atomic<int> role;


/* ===================================== */
/* Raft States  */
/* ===================================== */
/* Persistent State */


typedef struct logEntry_ {
    int commmand;
    int term;
}logEntry;

typedef struct persistStates_ {
    std::atomic<int> currentTerm{0};   // init to 0
    int votedFor = -1;  // init to -1
    int entryNum = 0;
    std::vector<logEntry> raftLog;
}persistStates;

persistStates pStates;

/* Volatile State on all servers */
int commitIndex; // init from 0
int lastApplied; 


/* Volatile State on leaders */
/* reinitialized after election */
int nextIndex[NODE_NUM];
int matchIndex[NODE_NUM];


class raft_rpcHandler : virtual public raft_rpcIf {
public:
    raft_rpcHandler() {
        std::cout << "Raft Node Started" << std::endl;
    }

    void ping() {
        printf("%n: raft ping n", &myID);
    }

    void request_vote(request_vote_reply& ret, const request_vote_args& requestVote);
    void append_entries(append_entries_reply& ret, const append_entries_args& appendEntries);
};
