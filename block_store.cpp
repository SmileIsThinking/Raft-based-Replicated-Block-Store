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

int server = 0;
std::shared_ptr<blob_rpcClient> client;

void BlockStore::conn_init(const std::string& hostname, const int port) {
    std::shared_ptr<TTransport> socket(new TSocket(hostname, port));
    std::shared_ptr<TTransport> transport(new TBufferedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    client = std::make_shared<blob_rpcClient>(protocol);
    transport->open();
    client->ping();
}

// note that if unexpected is returned, the client will retry until timeout
Errno::type BlockStore::read(const int64_t address, std::string& value, int retry_time, int sleep_time) { //
    int try_time = 0;
    std::string read_str;
    read_ret ret_res;

    while(try_time < retry_time){
        try_time++;
        try{
            conn_init(addr(server), blob_port(server));
            client->read(ret_res, address);

            if(ret_res.rc == Errno::SUCCESS) {
                value = ret_res.value;
                return Errno::SUCCESS;
            } else if(ret_res.rc == Errno::BACKUP){
                server = 1 - server;
                std::cout<<"reconnect to node "<<server<<std::endl;
            }
        } catch (TException &tx){
            // dosth
            server = 1 - server;
            sleep(sleep_time);
        }
    }
    return ret_res.rc;
}

Errno::type BlockStore::write(const int64_t address, std::string& write, int retry_time, int sleep_time) {
    Errno::type error;
    std::cout<< "start write: "<<write.substr(0, 10);
    int tries = retry_time;
    while(tries > 0){
        tries--;
        try{
            conn_init(addr(server), blob_port(server));
            error = client->write(address, write);
            std::cout<<error<<std::endl;;

            if(error == Errno::BACKUP){
                // reconnect to backup, change host
                server = 1 - server;
                std::cout<<"reconnect to node "<<server<<std::endl;
            } else if(error == Errno::SUCCESS){
                return error;
            }
        } catch (TException &tx){
            // dosth
            server = 1 - server;
            sleep(sleep_time);
        }
    }
    return error;
}
