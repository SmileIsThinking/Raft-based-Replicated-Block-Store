#include "gen-cpp/blob_rpc.h"
#include "gen-cpp/raft_rpc.h"

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/async/TConcurrentClientSyncInfo.h>

#include <random>
#include <thread>
#include <iostream>
#include <time.h>
#include <chrono>

#include "include.h"
#include "server_store.h"
#include <vector>
#include "util.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::concurrency;
using namespace ::apache::thrift::server;


// #define APPEND_TIMEOUT  10 
// does not receive appendEntry in timeout and convert to candidate
#define ELECTION_TIMEOUT  3000 // gap between different requestVote rpc
#define HB_FREQ 2000 // The frequency of sending appendEntries RPC in leader
int64_t last_election;
int64_t REAL_TIMEOUT;

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<> dist(0, ELECTION_TIMEOUT);


pthread_rwlock_t applylock;
std::string my_addr;

// std::atomic<bool> pending_backup;
std::atomic<bool> is_primary;
// std::atomic<bool> is_leader;
int64_t last_heartbeat;
// assuming single backup
std::atomic<int> num_write_requests;
std::atomic<bool> pending_candidate;


class blob_rpcHandler : virtual public blob_rpcIf {
public:
    blob_rpcHandler() {
        std::cout << "Blob Server Started" << std::endl;
    }

    void ping() {
        printf("%s: blob_ping\n", is_primary ? "primary" : "backup");
    }

    void read(request_ret& _return, const int64_t addr);
    void write(request_ret& _return, const int64_t addr, const std::string& value);

    void compareLogs();
    void compareBlock(const int64_t addr);
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
pthread_rwlock_t rolelock;
std::atomic<int> leaderID;

pthread_rwlock_t raftloglock;

/* ===================================== */
/* Raft States  */
/* ===================================== */
/* Persistent State */

std::atomic<int> currentTerm{0};   // init to 0
std::atomic<int> votedFor{-1};  // init to -1
// std::atomic<int> entryNum{0};  
std::vector<entry> raftLog = {};  // log index starts from 0!!!
// TODO: log vector lock?


/* Volatile State on all servers */
std::atomic<int> commitIndex; // init from -1
std::atomic<int> lastApplied; 


/* Volatile State on leaders */
/* reinitialized after election */
int nextIndex[NODE_NUM];
int matchIndex[NODE_NUM];


class raft_rpcHandler : virtual public raft_rpcIf {
public:
    raft_rpcHandler() {
        std::cout << "Raft Node Started" << std::endl;
    }

    void ping(int other);
    void request_vote(request_vote_reply& ret, const request_vote_args& requestVote);
    void append_entries(append_entries_reply& ret, const append_entries_args& appendEntries);

    void compareTest(const std::vector<entry> & leaderLog, \
    const int32_t leaderTerm, const int32_t leaderVote);
    void blockTest(const int64_t address, const std::string& value);
};

void new_request(request_ret& _return, entry e);

void raft_rpc_init();
void toFollower(int term);
void toCandidate();
void toLeader();

void send_request_votes();
void send_appending_requests();

void entry_format_print(entry logEntry);
void applyToStateMachine();

void new_request(request_ret& _return, entry e);

bool compare_one_log(const entry& e1, entry& e2);
bool compare_log_vector(const std::vector<entry>& log1, std::vector<entry>& log2);