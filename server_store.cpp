#include "server_store.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <iostream>
#include <fstream>

int fd = -1;
int log_fd = -1;
int state_fd = -1;


int curr_seq = 0;

const int entrySize = 2 * sizeof(int32_t) + sizeof(int64_t) + BLOCK_SIZE;

pthread_rwlock_t rwlock;

pthread_rwlock_t loglock;
pthread_rwlock_t statelock;

std::ifstream logIn;
std::ofstream logOut;
    
int ServerStore::init(int node_id) {
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



    pthread_rwlock_init(&statelock, NULL);
    filename = STATE + std::to_string(node_id);
    std::cout << filename << std::endl;
    state_fd = open(filename.c_str(), O_RDWR | O_CREAT, mode);
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

int ServerStore::write(const int64_t addr, const std::string& value, int64_t& seq) {
    std::cout << "write(" << addr << ", " << value << ")" << std::endl;
    int ret = pthread_rwlock_wrlock(&rwlock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }
    pwrite(fd, value.c_str(), BLOCK_SIZE, addr);
    seq = curr_seq;
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

int ServerStore::append_log(entry& logEntry) {
    std::string s(BLOCK_SIZE, ' ');
    if(logEntry.command == 0) {
        logEntry.content = s;
    }
    std::cout << "size: " << sizeof(logEntry) << std::endl;

    // lock
    int ret = pthread_rwlock_wrlock(&loglock);
    if (ret != 0) {
        std::cout << "STATE LOCK ERROR!" << std::endl;
        return -1;
    }
    
    logOut.write(reinterpret_cast<const char *>(&logEntry.command), sizeof(logEntry.command));
    logOut.write(reinterpret_cast<const char *>(&logEntry.term), sizeof(logEntry.term));
    std::cout << sizeof(logEntry.address) << std::endl;
    logOut.write(reinterpret_cast<const char *>(&logEntry.address), sizeof(logEntry.address));
    logOut.write(logEntry.content.c_str(), BLOCK_SIZE);
    // logOut.close();

    // unlock
    pthread_rwlock_unlock(&loglock);

    return 0;
}

int ServerStore::read_log(int index, entry& logEntry) {

    // lock
    int ret = pthread_rwlock_rdlock(&loglock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }

    // std::ifstream logIn;

    // logIn.open(LOG, std::ios::binary);
    logIn.seekg(index * entrySize, std::ios_base::beg);
    logIn.read(reinterpret_cast<char *>(&logEntry.command), sizeof(logEntry.command));
    logIn.read(reinterpret_cast<char *>(&logEntry.term), sizeof(logEntry.term));
    logIn.read(reinterpret_cast<char *>(&logEntry.address), sizeof(logEntry.address));
    char c[BLOCK_SIZE];
    logIn.read(c, BLOCK_SIZE);
    std::string s(c);
    std::cout << s << std::endl;
    logEntry.content = s;

    // logIn.close();

    //unlock
    pthread_rwlock_unlock(&loglock);

    return 0;
}

int ServerStore::write_state(int currentTerm, int votedFor) {
    int ret = pthread_rwlock_wrlock(&statelock);
    if (ret != 0) {
        std::cout << "STATE LOCK ERROR!" << std::endl;
        return -1;
    }

    std::string s = std::to_string(currentTerm) + " " + std::to_string(votedFor);
    // lseek(state_fd, 0, SEEK_SET);
    
    int len = s.size();
    pwrite(state_fd, s.c_str(), len, 0);
    pthread_rwlock_unlock(&statelock);

    return 0;
}

int ServerStore::read_state(int* currentTerm, int* votedFor) {
    struct stat fileStat;
    int ret = pthread_rwlock_rdlock(&statelock);
    if (ret != 0) {
        std::cout << "LOCK ERROR!" << std::endl;
        return -1;
    }

    fstat(state_fd, &fileStat);
    char* buf = new char[fileStat.st_size + 1];
    pread(state_fd, buf, fileStat.st_size + 1, 0);
    pthread_rwlock_unlock(&rwlock);

    std::string s(buf);
    std::string token;
    size_t pos = 0;
    std::string delimiter = " ";
    while ((pos = s.find(" ")) != std::string::npos) {
        token = s.substr(0, pos);
        *currentTerm = std::stoi(token);
        s.erase(0, pos + delimiter.length());
    }    
    *votedFor = std::stoi(s);
    delete[] buf;

    return 0;
}