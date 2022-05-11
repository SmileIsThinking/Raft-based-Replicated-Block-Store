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

test1: thrift Test1.cpp block_store.cpp block_store.h
	${CC} ${CFLAGS} -o test1 Test1.cpp block_store.cpp util.cpp ${DEP} ${LIB}

clean:
	rm -rf gen-cpp/ server client
