#include "server_store.h"
#include "util.h"

void gen1(int ID){
    ServerStore::init(ID);
    std::vector<entry> logEntries = {};
    entry e;
    std::string s;
    stringGenerator(s, 4096);
    e.content = s;
    e.address = 0;
    e.command = 0;
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
    std::string s;
    stringGenerator(s, 4096);
    e.content = s;
    e.address = 0;
    e.command = 0;
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
     std::string s;
    stringGenerator(s, 4096);
    e.content = s;
    e.address = 0;
    e.command = 0;
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
    return 0;
}

