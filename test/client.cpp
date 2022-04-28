#include <sstream>
#include <cstring>
#include "block_store.h"
#include "ClientState.h"
#include <time.h> // clock_gettime
#include "util.h"

#define BLOCK_SIZE 4096
#define TEST_NUM 100

int max_tries = 6;
int sleep_time = 1;


long read_perf[TEST_NUM] = {0};
long write_perf[TEST_NUM] = {0};

float average(long* array) {
    long sum = 0;
    for(int i = 0; i < TEST_NUM; i++){
        sum += array[i];
    }
    float average = (float)(sum) / TEST_NUM;
    return average;
}

int64_t str_to_int(const std::string& number){
    int64_t value;
    std::istringstream iss(number);
    iss >> value;
    return value;
}

int read(const int64_t address, std::string& _return){
    int res = BlockStore::read(address, _return, max_tries, sleep_time);
    return res;
}

int write(const int64_t address, std::string& write){
    Errno::type res = BlockStore::write(address, write, max_tries, sleep_time);
    return res;
}

void padding(std::string& string, int size){
    if(string.size() == size){
        return;
    }
    if(string.size() > size){
        string = string.substr(0, size);
        std::cout << "write size is greater than 4K, has been trimed" << std::endl;
    }
    string.insert(string.size(), size - string.size(), '\0');
}

void shell(){
    std::string input = "";
    std::string args = "";
    while (input != "quit"){
        //Print menu
        std::cout << "\n\n********\nMenu\n********" << std::endl
                  << "  read <path to file on server>" << std::endl
                  << "  (p)write <text>" << std::endl
                  << "  quit" << std::endl;

        std::cout << "$ ";
        std::cin >> input;
        std::cout << std::endl;

        if (!std::cin){
            std::cin.clear();
            std::cin.ignore(10000,'\n');
        }

        std::getline(std::cin, args);
        if (args.length() > 1) args = args.substr(1, args.length());

        // Make server calls when necessary
        try{
            if (input == "read") {
                std::string buff;
                int ret_code;
                // note that if there's no numbers, the result will be 0
                int64_t value;
                std::istringstream iss(args);
                iss >> value;
                ret_code = read(value, buff);
                if (ret_code != 0){
                    std::cout << "ErrNo: " << value << std::endl;
                    perror("open() returned ");
                    std::cout << std::endl;
                }
                std::cout << buff.substr(0, 10) << std::endl;
            } else if (input == "write") {
                int resp = write(0, args);
                if (resp < 0){
                    std::cout << "ErrNo: " << errno << std::endl;
                    perror("write() returned ");
                    std::cout << std::endl;
                }
            } else if (input == "pwrite") {
            }  else if (input != "quit"){
                std::cout << "\nCommand Not Recognized\n";
            }
        }catch (apache::thrift::TException &tx){
            std::cout<<"Error: " <<tx.what() <<std::endl;
        }
    }
}

int main(int argc, char** argv) {
    ClientState clientState;
    if (argc == 2){
        max_tries = std::stoi(argv[2]);
    } else if (argc == 3){
        max_tries = std::stoi(argv[2]);
        sleep_time = std::stoi(argv[3]);
    }
    std::string s;
    std::string read_val;
    stringGenerator(s, BLOCK_SIZE);

    long elapsed_time = 0;
    long sec = 0;
    struct timespec* t = (struct timespec*)malloc(sizeof(struct timespec));
    struct timespec* u = (struct timespec*)malloc(sizeof(struct timespec));


    // change addr dynamically according to the tests
    int addr = 0;

    // write performance test
    clock_gettime(CLOCK_MONOTONIC, t);
    for(int i = 0; i < TEST_NUM; i++) {
        write(addr, s);
    }
    clock_gettime(CLOCK_MONOTONIC, u);
    elapsed_time = u->tv_nsec - t->tv_nsec;
    sec = (long)(u->tv_sec) - (long)(t->tv_sec);
    std::cout <<" write time: " << elapsed_time << std::endl;
    write_perf[0] = elapsed_time + sec*(1000000000);

    // read performance test
    clock_gettime(CLOCK_MONOTONIC, t);
    for(int i = 0; i < TEST_NUM; i++) {
        read(addr, read_val);
    }
    clock_gettime(CLOCK_MONOTONIC, u);
    elapsed_time = u->tv_nsec - t->tv_nsec;
    sec = (long)(u->tv_sec) - (long)(t->tv_sec);
    std::cout <<" read time: " << elapsed_time << std::endl;
    read_perf[0] = elapsed_time + sec*(1000000000);


    float write_avg, read_avg;
    write_avg = average(write_perf);
    read_avg = average(read_perf);

    std::cout << "WRITE PERF: (ns)" << std::endl;
    std::cout << write_avg << std::endl;
    std::cout << "READ PERF: (ns)" << std::endl;
    std::cout << read_avg << std::endl;

    return 0;
}
