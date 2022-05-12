#include "server_store.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <iostream>
#include <fstream>

int fd = -1;
int log_fd = -1;
int log_num_fd = -1;
int state_fd = -1;

int curr_id = -1;

int curr_seq = 0;

const int entrySize = 2 * sizeof(int32_t) + sizeof(int64_t) + BLOCK_SIZE;

std::string state_file;
pthread_rwlock_t rwlock;

pthread_rwlock_t loglock;
pthread_rwlock_t statelock;

std::ifstream logIn;
std::ofstream logOut;
// std::ifstream logNumIn;
// std::ofstream logNumOut;

bool is_empty(std::ifstream& pFile)
{
    return pFile.peek() == std::fstream::traits_type::eof();
}

int ServerStore::init(int node_id) {
    curr_id = node_id;
    pthread_rwlock_init(&rwlock, NULL);
    mode_t mode = S_IRUSR | S_IWUSR;
    std::string filename = STORE + std::to_string(node_id);
    std::cout << filename << std::endl;
    fd = open(filename.c_str(), O_RDWR | O_CREAT, mode);
    if (fd == -1) {
        return -1;
    }
    curr_seq = 0;
    

    pthread_rwlock_init(&loglock, NULL);
    filename = LOG + std::to_string(node_id);
    std::cout << filename << std::endl;
    log_fd = open(filename.c_str(), O_RDWR | O_CREAT, mode);
    if (log_fd == -1) {
        return -1;
    }

    logIn.open(filename, std::ios::binary);
    logOut.open(filename, std::ios::app | std::ios::binary);


    pthread_rwlock_init(&loglock, NULL);
    filename = LOG_NUM + std::to_string(node_id);
    std::cout << filename << std::endl;
    log_num_fd = open(filename.c_str(), O_RDWR | O_CREAT, mode);
    if (log_num_fd == -1) {
        return -1;
    }
    pthread_rwlock_wrlock(&loglock);

    struct stat fileStat;
    fstat(log_num_fd, &fileStat);
    if(fileStat.st_size == 0) {
        std::string s = std::to_string(0);
        int len = (int) s.size();
        std::cout << "line 75: " << len << std::endl;
        pwrite(log_num_fd, s.c_str(), len, 0);
    }
    pthread_rwlock_unlock(&loglock);

    pthread_rwlock_init(&statelock, NULL);
    state_file = STATE + std::to_string(node_id);
    std::cout << state_file << std::endl;
    state_fd = open(state_file.c_str(), O_RDWR | O_CREAT, mode);
    if (state_fd == -1) {
        return -1;
    }

    return 0;
}

int ServerStore::read(const int64_t addr, std::string& value) {
    std::cout << "read(" << addr << ")" << std::endl;
    char* buf = new char[BLOCK_SIZE];
    int ret = pthread_rwlock_rdlock(&rwlock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }
    pread(fd, buf, BLOCK_SIZE, addr);
    pthread_rwlock_unlock(&rwlock);
    value = std::string(buf);
    delete[] buf;
    return 0;
}

int ServerStore::write(const int64_t addr, const std::string& value) {
    std::cout << "write(" << addr << ", " << value << ")" << std::endl;
    int ret = pthread_rwlock_wrlock(&rwlock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }
    pwrite(fd, value.c_str(), BLOCK_SIZE, addr);
    // seq = curr_seq;
    curr_seq ++;
    pthread_rwlock_unlock(&rwlock);
    return 0;
}

int ServerStore::full_read(std::string& content) {
    struct stat fileStat;

    int ret = pthread_rwlock_rdlock(&rwlock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }

    fstat(fd, &fileStat);
    char* buf = new char[fileStat.st_size + 1];
    pread(fd, buf, fileStat.st_size + 1, 0);
    pthread_rwlock_unlock(&rwlock);

    content = std::string(buf);
    delete[] buf;
    return 0;
}

int ServerStore::full_write(std::string& content) {
    int ret = pthread_rwlock_wrlock(&rwlock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }

    ret = ftruncate(fd, 0);
    if (ret == -1) {
        perror("ftruncated failed\n");
        return -1;
    }

    int len = content.size();
    pwrite(fd, content.c_str(), len, 0);
    pthread_rwlock_unlock(&rwlock);
    return 0;
}


/* ===================================== */
/* Raft States Update */
/* ===================================== */

int ServerStore::append_log(const std::vector<entry>& logEntries) {
    if(logEntries.empty()) {
        std::cout << "ERROR: Entry Vector is EMPTY!!!" << std::endl;
        return -1;
    }
    std::string s(BLOCK_SIZE, ' ');

    // lock
    int ret = pthread_rwlock_wrlock(&loglock);
    if (ret != 0) {
        std::cout << "STATE LOCK ERROR!" << std::endl;
        return -1;
    }
    int pos = read_log_num() * entrySize;
    logOut.seekp(pos);

    for(auto logEntry: logEntries) {
        logOut.write(reinterpret_cast<const char *>(&logEntry.command), sizeof(logEntry.command));
        logOut.write(reinterpret_cast<const char *>(&logEntry.term), sizeof(logEntry.term));
        logOut.write(reinterpret_cast<const char *>(&logEntry.address), sizeof(logEntry.address));
        if (logEntry.command == 0)  {
            logOut.write(s.c_str(), BLOCK_SIZE);
        }else {
            logOut.write(logEntry.content.c_str(), BLOCK_SIZE);
        }
    }
    
    // IMPORTANT: keep consistency
    // write log entry first, log num second
    std::cout << "log entry size: " << (int) logEntries.size() << std::endl;
    int num = read_log_num() + (int) logEntries.size();
    std::string ss = std::to_string(num);
    int len = ss.size();
    pwrite(log_num_fd, ss.c_str(), len, 0);

    // unlock
    pthread_rwlock_unlock(&loglock);

    return 0;
}

entry ServerStore::read_log(int index) {
    if(index < 0) {
        std::cout << "Invalid index" << std::endl;
    }
    // lock
    entry logEntry;
    int ret = pthread_rwlock_rdlock(&loglock);
    if (ret != 0) {
        std::cout << "LOCK ERROR! return empty entry" << std::endl;
        return logEntry;
    }

    
    logIn.seekg(index * entrySize, std::ios_base::beg);
    logIn.read(reinterpret_cast<char *>(&logEntry.command), sizeof(logEntry.command));
    logIn.read(reinterpret_cast<char *>(&logEntry.term), sizeof(logEntry.term));
    logIn.read(reinterpret_cast<char *>(&logEntry.address), sizeof(logEntry.address));
    char c[BLOCK_SIZE];
    logIn.read(c, BLOCK_SIZE);
    std::string s(c);
    logEntry.content = s;

    //unlock
    pthread_rwlock_unlock(&loglock);

    return logEntry;
}

int ServerStore::read_log_num() {
    struct stat fileStat;

    fstat(log_num_fd, &fileStat);
    char* buf = new char[fileStat.st_size + 1];
    pread(log_num_fd, buf, fileStat.st_size + 1, 0);  
    return (int) strtol(buf,NULL,10); //atoi(buf);
}

std::vector<entry> ServerStore::read_full_log() {
    int log_num = read_log_num();
    std::vector<entry> logEntries;
    logEntries.reserve(log_num);
    for(int i = 0; i < log_num; i++) {
        logEntries.emplace_back(read_log(i));
    }
    return logEntries;
}

// include this index, log[index] and following logs would be removed.
int ServerStore::remove_log(int index) {
    int ret = pthread_rwlock_wrlock(&loglock);
    if (ret != 0) {
        std::cout << "STATE LOCK ERROR!" << std::endl;
        return -1;
    }

    std::string ss = std::to_string(index);
    int len = (int) ss.size();
    pwrite(log_num_fd, ss.c_str(), len, 0);
    std::cout << "log num: " << ss << std::endl;
    // unlock
    pthread_rwlock_unlock(&loglock);

    return 0;
}

int ServerStore::write_state(int currentTerm, int votedFor) {
    std::cout << "start write state" << std::endl;
    int ret = pthread_rwlock_wrlock(&statelock);
    if (ret != 0) {
        std::cout << "STATE LOCK ERROR!" << std::endl;
        return -1;
    }
    // std::cout << "lock ends" << std::endl;
    std::string s = std::to_string(currentTerm) + " " + std::to_string(votedFor);
    std::cout << "Server Store write states: " << votedFor << std::endl;
    int len = (int) s.size();
//    pwrite(state_fd, s.c_str(), len, 0);
    std::ofstream write_state_file(STATE+std::to_string(curr_id),std::ofstream::trunc);
    write_state_file << s;
    write_state_file.close();
    pthread_rwlock_unlock(&statelock);

    return 0;
}

int ServerStore::read_state(int* currentTerm, int* votedFor) {
    struct stat fileStat;

    if(stat(state_file.c_str(), &fileStat) == 0) {
        std::cout << "STATE DOES NOT EXIST" << std::endl;
        return -1;
    }
    int ret = pthread_rwlock_rdlock(&statelock);

    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }

    fstat(state_fd, &fileStat);
    char* buf = new char[fileStat.st_size + 1];
    pread(state_fd, buf, fileStat.st_size + 1, 0);
    pthread_rwlock_unlock(&statelock);

    std::string s(buf);
    std::string token;
    size_t pos = 0;
    std::string delimiter = " ";
    while ((pos = s.find(' ')) != std::string::npos) {
        token = s.substr(0, pos);
        *currentTerm = std::stoi(token);
        s.erase(0, pos + delimiter.length());
    }    
    *votedFor = std::stoi(s);
    delete[] buf;

    return 0;
}