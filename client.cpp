/////////////////////////////////////////////////////////////////
/**
 * Running the test -
 * 1. specify the location of break
 * 2. command argument: server init state, timeout(print to console every second),
 *                                          random factors (range)
 * 3. check output, states.
 * 4.
 */



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
            } else if (input == "check") {
                check_consistency(init_leader);
            }  else if (input != "quit"){
                std::cout << "\nCommand Not Recognized\n";
            }
        }catch (apache::thrift::TException &tx){
            std::cout<<"Error: " <<tx.what() <<std::endl;
        }
    }
//    delete afesq;
}

int main(int argc, char** argv) {
    ClientState clientState;
    if (argc == 2){
        init_leader = std::stoi(argv[1]);
    } else if (argc == 3){
        max_tries = std::stoi(argv[1]);
        sleep_time = std::stoi(argv[2]);
    }
    std::string s;
    stringGenerator(s, 4096);
    // std::cout<<"client start" <<std::endl;
    // padding(s, 4096);
    // std::cout<<"size of the string: "<<s.size()<<std::endl;
    write(0, s);
    // std::string read_val;
    // read(0, read_val);
    // std::cout<<"str read: "<<read_val.substr(0, 10) << std::endl;
    // shell();
    return 0;
}
