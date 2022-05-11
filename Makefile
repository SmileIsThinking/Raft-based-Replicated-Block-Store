CC := g++
CFLAGS := -Wall -g -std=c++11
LIB := -lthrift -pthread

DEP := gen-cpp/blob_rpc.cpp gen-cpp/raft_rpc.cpp gen-cpp/rpc_types.cpp

all: server client

server: thrift server.cpp server.h server_store.cpp server_store.h
	${CC} ${CFLAGS} -o server server.cpp server_store.cpp util.cpp ${DEP} ${LIB}

client: thrift client.cpp block_store.cpp block_store.h
	${CC} ${CFLAGS} -o client client.cpp block_store.cpp util.cpp ${DEP} ${LIB}

thrift: rpc.thrift
	thrift --gen cpp rpc.thrift

clean:
	rm -rf gen-cpp/ server client

remove:
	rm -rf BLOCK_STORE0 LOG_NUM0 LOG0 STATE0 BLOCK_STORE1 LOG_NUM1 LOG1 STATE1 BLOCK_STORE2 LOG_NUM2 LOG2 STATE2
