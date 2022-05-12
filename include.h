#include <string>

#define BLOB_SERVER_WORKER 5
#define PB_SERVER_WORKER 5
#define RAFT_SERVER_WORKER 5

/* ===================================== */
/* Raft Consts  */
/* ===================================== */

#define NODE_NUM 3
#define MAJORITY ((NODE_NUM/2)+1)
const std::string nodeAddr[NODE_NUM] = {"10.10.1.1", "10.10.1.3", "10.10.1.2"};
const int raftPort[NODE_NUM] = {9090, 9091, 9092};
const int cliPort[NODE_NUM] = {9093, 9094, 9095};
