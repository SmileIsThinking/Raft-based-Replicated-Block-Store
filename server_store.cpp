#include "server_store.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <iostream>
#include <atomic>

int fd = -1;

enum LockType {
    READ = 0,
    WRITE = 1,
};

struct block {
    block() = default;
    block(block&&): in_use(false) {};
    bool in_use = false;
    std::atomic_int64_t curr_seq;
    pthread_rwlock_t lock;
};

std::vector<block> blocks;
pthread_mutex_t file_lock;

// assume already acquired the appropriate lock
void lock_helper(struct block& block, const enum LockType type) {
    if (type == LockType::READ)
        pthread_rwlock_rdlock(&block.lock);
    else
        pthread_rwlock_wrlock(&block.lock);
}

// assume already acquired the appropriate lock
void init_block(struct block& block, int64_t seq = 0) {
    pthread_rwlock_init(&block.lock, NULL);
    block.curr_seq = seq;
    block.in_use = true;
}

void lock_aquire(const int64_t addr, const enum LockType type) {

    if (block_id(addr + BLOCK_SIZE) >= blocks.size()) {
        // allocate space for blocks
        pthread_mutex_lock(&file_lock);
        blocks.resize(block_id(addr + BLOCK_SIZE - 1) + 1);
        init_block(blocks[block_id(addr + BLOCK_SIZE)]);
        if (!blocks[block_id(addr)].in_use)
            init_block(blocks[block_id(addr)]);
        pthread_mutex_unlock(&file_lock);
    } else if (!blocks[block_id(addr)].in_use || !blocks[block_id(addr + BLOCK_SIZE - 1)].in_use) {
        // blocks are not initialized yet
        pthread_mutex_lock(&file_lock);
        if (!blocks[block_id(addr)].in_use)
            init_block(blocks[block_id(addr)]);
        if (!blocks[block_id(addr + BLOCK_SIZE - 1)].in_use)
            init_block(blocks[block_id(addr + BLOCK_SIZE - 1)]);
        pthread_mutex_unlock(&file_lock);
    }

    for (int i = block_id(addr); i <= block_id(addr + BLOCK_SIZE - 1); i++)
        lock_helper(blocks[i], type);

}

void lock_release(const int64_t addr) {
    for (int i = block_id(addr); i <= block_id(addr + BLOCK_SIZE - 1); i++)
        pthread_rwlock_unlock(&blocks[i].lock);
}

// assuming no concurrent access yet
int ServerStore::init(int node_id) {
    pthread_mutex_init(&file_lock, NULL);

    mode_t mode = S_IRUSR | S_IWUSR;
    std::string filename = STORE + std::to_string(node_id);
    std::cout << filename << std::endl;
    fd = open(filename.c_str(), O_RDWR | O_CREAT, mode);
    if (fd == -1)
        return -1;

    struct stat stat_buf;
    if (fstat(fd, &stat_buf))
        return -1;
    size_t num_blocks = block_id(stat_buf.st_size) + 1;
    blocks = std::vector<block>(num_blocks);
    for (auto& block : blocks)
        init_block(block);

    return 0;
}

int ServerStore::read(const int64_t addr, std::string& value) {
    std::cout << "read(" << addr << ")" << std::endl;
    char buf[BLOCK_SIZE];

    lock_aquire(addr, LockType::READ);
    int ret = pread(fd, &buf, BLOCK_SIZE, addr);
    lock_release(addr);

    value = std::string(buf);
    return ret;
}

int ServerStore::write(const int64_t addr, const std::string& value, std::vector<int64_t>& seq) {
    std::cout << "write(" << addr << ", " << value << ")" << std::endl;

    lock_aquire(addr, LockType::WRITE);
    int ret = pwrite(fd, value.c_str(), BLOCK_SIZE, addr);
    for (int i = block_id(addr); i <= block_id(addr + BLOCK_SIZE - 1); i++) {
        seq.push_back(blocks[i].curr_seq);
        blocks[i].curr_seq++;
    }
    lock_release(addr);
    return ret;
}

// this function is only used during new_backup - concurrency handled in server.cpp
int ServerStore::full_read(std::string& content, std::vector<int64_t>& seq) {
    struct stat fileStat;

    if (fstat(fd, &fileStat))
        return -1;

    char buf[fileStat.st_size + 1];
    int ret = pread(fd, &buf, fileStat.st_size, 0);
    content = std::string(buf);
    size_t num_blocks = block_id(fileStat.st_size) + 1;
    for (size_t i = 0; i < num_blocks; i++)
        seq.emplace_back(blocks[i].curr_seq.load());
    return ret;
}

// this function is only used during backup initialization - no concurrency due to single primary
int ServerStore::full_write(const std::string& content, const std::vector<int64_t>& seq) {

    if (ftruncate(fd, 0) == -1) {
        perror("ftruncated failed\n");
        return -1;
    }

    int ret = pwrite(fd, content.c_str(), content.size(), 0);
    size_t num_blocks = block_id(content.size()) + 1;
    blocks = std::vector<block>(num_blocks);
    for (size_t i = 0; i < num_blocks; i++)
        init_block(blocks[i], seq[i]);
    return ret;
}

void ServerStore::wait_prev(const int64_t addr, const std::vector<int64_t>& seq) {
    for (int i = block_id(addr), j = 0; i <= block_id(addr + BLOCK_SIZE - 1); i++, j++)
        while (blocks[i].curr_seq < seq[j]);
}
