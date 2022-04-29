#include "server_store.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <iostream>

int fd = -1;
int curr_seq = 0;
pthread_rwlock_t rwlock;

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
