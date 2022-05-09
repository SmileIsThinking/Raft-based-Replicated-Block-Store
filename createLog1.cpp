#include "server_store.h"

int main(int argc, char** argv){
    ServerStore::init(1);
    entry e;
    std::vector<entry> logEntries;
  
    e.term = 1;
    logEntries.emplace_back(e);
    // logEntries.emplace_back(e);
   
    e.term = 2;
    logEntries.emplace_back(e);


    ServerStore::append_log(logEntries);
    return 0;
}