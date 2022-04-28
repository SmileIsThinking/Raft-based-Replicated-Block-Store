#include <string>
#include <vector>

#define STORE "BLOCK_STORE"
#define BLOCK_SHIFT 12
#define BLOCK_SIZE (1 << BLOCK_SHIFT)
#define block_id(x) ((x) >> BLOCK_SHIFT)
// int fd = -1;

namespace ServerStore {

int init(int node_id);
int read(const int64_t addr, std::string& value);
int write(const int64_t addr, const std::string& value, std::vector<int64_t>& seq);
int full_read(std::string& content, std::vector<int64_t>& seq);
int full_write(const std::string& content, const std::vector<int64_t>& seq);
void wait_prev(const int64_t addr, const std::vector<int64_t>& seq);

}
