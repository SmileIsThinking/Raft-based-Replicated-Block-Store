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
    // std::cout<<"start to read a block" <<std::endl;
    int res = BlockStore::read(address, _return, init_leader, max_tries, sleep_time);
    return res;
}

int write(const int64_t address, std::string& write){
    Errno::type res = BlockStore::write(address, write, init_leader, max_tries, sleep_time);
    // std::cout<<"write returned" <<std::endl;
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
            int64_t value;
            std::istringstream iss(args);
            iss >> value;
            if (input == "read") {
                std::string buff;
                int ret_code;
                // note that if there's no numbers, the result will be 0
                
                ret_code = read(value, buff);
                if (ret_code != 0){
                    std::cout << "ErrNo: " << value << std::endl;
                    perror("open() returned ");
                    std::cout << std::endl;
                }
                std::cout << buff.substr(0, 10) << std::endl;
            } else if (input == "write") {
                std::string s;
                stringGenerator(s, 4096);
                int resp = write(value, s);
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

void test1(int addr) {
    std::string s;
    stringGenerator(s, 4096);
   
    struct timespec start, medium, end;
    std::string read_val;
    double accum1, accum2;
    accum1 = 0;
    accum2 = 0;
    for(int i=0; i < 100; i++){
        clock_gettime( CLOCK_REALTIME, &start);
        write(addr, s);
        clock_gettime( CLOCK_REALTIME, &medium);
        
        read(addr, read_val);
        clock_gettime( CLOCK_REALTIME, &end);

        accum1 += ((double)medium.tv_sec * 1000 + 1.0e-6*medium.tv_nsec) - 
        ((double)start.tv_sec * 1000 + 1.0e-6*start.tv_nsec);
        accum2 += ((double)end.tv_sec * 1000 + 1.0e-6*end.tv_nsec) - 
        ((double)medium.tv_sec * 1000 + 1.0e-6*medium.tv_nsec);

    }
       
    // std::cout << "total time is: " << accum1  << "ms"<<       std::endl;
    std::cout << "write time is: " << accum1 /100 << "ms"<<       std::endl;

    std::cout << "read time is: " << accum2 /100  << "ms"<< std::endl;

}

void test3(){
    std::string s;
    stringGenerator(s, 4096);
    
    struct timespec start, end;
    std::string read_val;
    double accum;
    accum = 0;
    write(0, s);
    for(int i=0; i < 1000; i++){
        
        clock_gettime( CLOCK_REALTIME, &start);
        read(0, read_val);
        clock_gettime( CLOCK_REALTIME, &end);

        accum += ((double)end.tv_sec * 1000 + 1.0e-6*end.tv_nsec) - 
        ((double)start.tv_sec * 1000 + 1.0e-6*start.tv_nsec);

    }
    std::cout << " avg time is: " << accum/1000  << "ms"<<  std::endl;
    
}

void test2(){
    std::string s;
    stringGenerator(s, 4096);
    
    struct timespec start, medium, end;
    std::string read_val;
    double accum1, accum2;
    accum1 = 0;
    accum2 = 0;
    std::vector<int> a = {10, 30, 50, 70, 90};
    clock_gettime( CLOCK_REALTIME, &start);
    for(int k : a) {
        for(int i=0; i < k; i++){
        
        write(0, s);
        // std::cout << "writing number " << i << std::endl;
        }
        for (int j=0; j < (100-k); j++){
            read(0, read_val);
            // std::cout << "reading number " << j << std::endl;
        }
        
        
        clock_gettime( CLOCK_REALTIME, &end);

            accum1 = ((double)end.tv_sec * 1000 + 1.0e-6*end.tv_nsec) - 
            ((double)start.tv_sec * 1000 + 1.0e-6*start.tv_nsec);
        
        
        std::cout << "write num " << k << " total time is: " << accum1  << "ms"<<  std::endl;
    }
    
    

}

int main(int argc, char** argv) {
    ClientState clientState;
    if (argc == 2){
        init_leader = std::stoi(argv[1]);
    } else if (argc == 3){
        max_tries = std::stoi(argv[1]);
        sleep_time = std::stoi(argv[2]);
    }
    // test1(0);
    test3();
    // test2();
    return 0;
}
