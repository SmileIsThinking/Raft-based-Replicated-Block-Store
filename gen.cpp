#include "server_store.h"

void gen1(int ID){
    ServerStore::init(ID);
    std::vector<entry> logEntries = {};
    entry e;
    e.term = 1;
    logEntries.emplace_back(e);
    logEntries.emplace_back(e);
    e.term = 2;
    logEntries.emplace_back(e);
    e.term = 4;
    logEntries.emplace_back(e);
    ServerStore::append_log(logEntries);
}

void gen2(int ID){
    ServerStore::init(ID);
    std::vector<entry> logEntries = {};
    entry e;
    e.term = 1;
    logEntries.emplace_back(e);
    logEntries.emplace_back(e);
    e.term = 2;
    logEntries.emplace_back(e);
    ServerStore::append_log(logEntries);
}

void gen3(int ID){
    ServerStore::init(ID);
    std::vector<entry> logEntries = {};
    entry e;
    e.term = 1;
    logEntries.emplace_back(e);
    logEntries.emplace_back(e);
    e.term = 3;
    logEntries.emplace_back(e);
    logEntries.emplace_back(e);
    logEntries.emplace_back(e);
    ServerStore::append_log(logEntries);
}

int main(){
    gen1(0);
    gen2(1);
    gen3(2);
    return 0;
}

