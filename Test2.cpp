//
// Created by ZK Hao on 5/11/2022.
//
#include <sstream>
#include <cstring>
#include "block_store.h"
#include "ClientState.h"
#include "util.h"

int max_tries = 6;
int sleep_time = 1;
int init_leader = -1;

int64_t str_to_int(const std::string& number){
    int64_t value;
    std::istringstream iss(number);
    iss >> value;
    return value;
}

int read(const int64_t address, std::string& _return){
    std::cout<<"start to read a block" <<std::endl;
    int res = BlockStore::read(address, _return, init_leader, max_tries, sleep_time);
    return res;
}

int write(const int64_t address, std::string& write){
    Errno::type res = BlockStore::write(address, write, init_leader, max_tries, sleep_time);
    std::cout<<"write returned" <<std::endl;
    return res;
}

bool check_consistency(const int64_t address){
    BlockStore::compare_blocks(address, init_leader, max_tries, sleep_time);
    BlockStore::compare_logs(init_leader, max_tries, sleep_time);
    return true;
}

void padding(std::string& string, int size){
    if((int)string.size() == size){
        return;
    }
    if((int)string.size() > size){
        string = string.substr(0, size);
        std::cout << "write size is greater than 4K, has been trimed" << std::endl;
    }
    string.insert(string.size(), size - string.size(), '\0');
}

int main(int argc, char** argv) {
    ClientState clientState;
    if (argc == 2){
        init_leader = std::stoi(argv[1]);
    } else if (argc == 3){
        max_tries = std::stoi(argv[1]);
        sleep_time = std::stoi(argv[2]);
    }
    std::string read_res;
    std::string s;
    stringGenerator(s, 4096);
    write(0, s);
    // leader crash case: leader add entry -> sync to others with append entry -> gather result
    std::cout << "Waiting for consistent state" << std::endl;
    sleep(1);
    std::cout << "Starting crashing the leader" << std::endl;
    sleep(10);
    // first crash
    std::cout << "New leader should be elected, starting writing the new content" << std::endl;
    std::string first_crash("first crash");
    padding(first_crash, 4096);
    write(0, first_crash);
    std::cout << "Waiting for consistent state" << std::endl;
    sleep(1);
    read(0, read_res);
    std::cout << "Res should be first crash: " << read_res << std::endl;
    std::cout << "Recover the failed leader" << std::endl;
    sleep(10);
    read(0, read_res);
    std::cout << "Check if consistent after recovery, res should be \"first crash\": " << read_res << std::endl;
    std::string recover_leader("Recover leader");
    padding(recover_leader, 4096);
    write(0, recover_leader);
    // restart compare log res
    read(0, read_res);
    std::cout << "Check if consistent after new write, res should be \"Recover leader\": " << read_res << std::endl;
    check_consistency(0);
    // compare logs blocks
    return 0;
}
