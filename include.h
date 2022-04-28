#define NODE0_ADDR "localhost"
#define NODE1_ADDR "localhost"
#define NODE0_BLOB_PORT 9090
#define NODE1_BLOB_PORT 9091
#define NODE0_PB_PORT 9092
#define NODE1_PB_PORT 9093

#define addr(x) (x == 0 ? NODE0_ADDR : NODE1_ADDR)
#define blob_port(x) (x == 0 ? NODE0_BLOB_PORT : NODE1_BLOB_PORT)
#define pb_port(x) (x == 0 ? NODE0_PB_PORT : NODE1_PB_PORT)

