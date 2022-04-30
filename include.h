#include <string>

#define NODE0_ADDR "localhost"
#define NODE1_ADDR "localhost"
#define NODE0_BLOB_PORT 9090
#define NODE1_BLOB_PORT 9091
#define NODE0_PB_PORT 9092
#define NODE1_PB_PORT 9093

#define addr(x) (x == 0 ? NODE0_ADDR : NODE1_ADDR)
#define blob_port(x) (x == 0 ? NODE0_BLOB_PORT : NODE1_BLOB_PORT)
#define pb_port(x) (x == 0 ? NODE0_PB_PORT : NODE1_PB_PORT)

#define BLOB_SERVER_WORKER 5
#define PB_SERVER_WORKER 5
#define RAFT_SERVER_WORKER 5

/* ===================================== */
/* Raft Consts  */
/* ===================================== */

#define NODE_NUM 3
#define MAJORITY ((NODE_NUM/2)+1)
const std::string nodeAddr[NODE_NUM] = {"localhost", "localhost", "localhost"};
const int raftPort[NODE_NUM] = {9090, 9091, 9092};
const int cliPort[NODE_NUM] = {9093, 9094, 9095};
