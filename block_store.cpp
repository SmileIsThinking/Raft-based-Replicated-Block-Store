#include "block_store.h"
#include <sys/types.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <iostream>

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;

// which server we are currently talking to

int server;
std::shared_ptr<blob_rpcClient> client;

void BlockStore::conn_init(const std::string& hostname, const int port) {
    std::shared_ptr<TTransport> socket(new TSocket(hostname, port));
    std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    client = std::make_shared<blob_rpcClient>(protocol);
    transport->open();
    client->ping();
    std::cout<<"conn_init: "<<port<<std::endl;
}

// note that if unexpected is returned, the client will retry until timeout
Errno::type BlockStore::read(const int64_t address, std::string& value, int init_leader, int retry_time, int sleep_time) { //
    int try_time = 0;
    std::string read_str;
    request_ret ret_res;
    server = init_leader > -1 ? init_leader : (rand() * NODE_NUM) % NODE_NUM;
    while(try_time < retry_time){
        try_time++;
        try{
            std::cout<<"read connect to node "<<server<<std::endl;
            conn_init(nodeAddr[server], cliPort[server]);
            client->read(ret_res, address);

            if(ret_res.rc == Errno::SUCCESS) {
                value = ret_res.value;
                return Errno::SUCCESS;
            } else if(ret_res.rc == Errno::NOT_LEADER){
                server = ret_res.node_id > -1 ? ret_res.node_id : (rand() * NODE_NUM) % NODE_NUM;
                std::cout<<"reconnect to leader "<<server<<std::endl;
            }
        } catch (TException &tx){
            server = (rand() * NODE_NUM) % NODE_NUM;
            sleep(sleep_time);
        }
    }
    return ret_res.rc;
}

Errno::type BlockStore::write(const int64_t address, std::string& write, int init_leader, int retry_time, int sleep_time) {
    std::cout<< "start write: "<<write.substr(0, 10);
    int tries = retry_time;
    request_ret ret_res;
    // server = (rand() * NODE_NUM) % NODE_NUM;
    server = init_leader > -1 ? init_leader : (rand() * NODE_NUM) % NODE_NUM;
    while(tries > 0){
        tries--;
        try{
            std::cout<<"write connect to node "<<server<<std::endl;
            conn_init(nodeAddr[server], cliPort[server]);
            client->write(ret_res, address, write);
            std::cout<<ret_res.rc<<std::endl;;

            if(ret_res.rc == Errno::NOT_LEADER){
                // reconnect to backup, change host
                server = ret_res.node_id > -1 ? ret_res.node_id : (rand() * NODE_NUM) % NODE_NUM;
                std::cout<<"reconnect to node "<<server<<std::endl;
            } else if(ret_res.rc== Errno::SUCCESS){
                return ret_res.rc;
            }
        } catch (TException &tx){
            server = (rand() * NODE_NUM) % NODE_NUM;
            sleep(sleep_time);
        }
    }
    return ret_res.rc;
}

void BlockStore::compare_logs(int init_leader, int retry_time, int sleep_time){
    server = init_leader > -1 ? init_leader : (rand() * NODE_NUM) % NODE_NUM;
    int tries = retry_time;
    while(tries > 0){
        tries--;
        try{
            std::cout<<"starting comparing logs on leader with id: "<<server<<std::endl;
            conn_init(nodeAddr[server], cliPort[server]);
            client->compareLogs();
            return;
        } catch (TException &tx){
            server = (rand() * NODE_NUM) % NODE_NUM;
            sleep(sleep_time);
        }
    }
}

void BlockStore::compare_blocks(const int64_t address, int init_leader, int retry_time, int sleep_time){
    server = init_leader > -1 ? init_leader : (rand() * NODE_NUM) % NODE_NUM;
    int tries = retry_time;
    while(tries > 0){
        tries--;
        try{
            std::cout<<"starting comparing blocks on leader with id: "<<server<<std::endl;
            conn_init(nodeAddr[server], cliPort[server]);
            client->compareBlock(address);
            return;
        } catch (TException &tx){
            server = (rand() * NODE_NUM) % NODE_NUM;
            sleep(sleep_time);
        }
    }
}
