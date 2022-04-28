//
// Created by ZK Hao on 3/29/2022.
//

#ifndef INC_739_PROJECT_3_CLIENTSTATE_H
#define INC_739_PROJECT_3_CLIENTSTATE_H

#endif //INC_739_PROJECT_3_CLIENTSTATE_H
#include <map>
#include <iostream>
#include <utility>

struct client_node{
    bool dirty = false;
    std::string cache_path;
    std::string tmp_path;
};

class ClientState{
    std::map<int64_t, client_node> node_states;
    std::string host;

public:
    int add_file(int64_t file, std::string cache, std::string tmp){
        node_states[file].cache_path = std::move(cache);
        node_states[file].tmp_path = std::move(tmp);
        return 1;
    }
    bool isDirty(int64_t file){
        return node_states[file].dirty;
    }
    void setDirty(int64_t file){
        node_states[file].dirty = true;
    }
    void setHost(const std::string& host_){
        host = host_;
    }
    std::string getHost(){
        return host;
    }
};
