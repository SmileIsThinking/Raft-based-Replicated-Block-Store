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
    std::cout << "Starting first crash" << std::endl;
    sleep(10);
    // first crash
    std::string first_crash("first crash");
    padding(first_crash, 4096);
    write(0, first_crash);
    //
    sleep(1);
    read(0, read_res);
    std::cout << "Res should be first crash: " << read_res << std::endl;
    std::cout << "Starting second crash writ should be blocked" << std::endl;
    sleep(5);
    // second crash
    std::string second_crash("second crash");
    padding(second_crash, 4096);
    max_tries = 1;
    write(0, second_crash);
    // should return nothing
    max_tries = 6;
    std::cout << "Starting first recovery" << std::endl;
    sleep(10);
    // restart compare log res
    write(0, second_crash);
    read(0, read_res);
    // compare logs blocks
    return 0;
}