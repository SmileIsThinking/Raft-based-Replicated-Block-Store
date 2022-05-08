#include <iostream>
#include <unistd.h>
#include "util.h"

void stringGenerator(std::string& str, int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    str.reserve(len);
    srand (time(NULL));
    for (int i = 0; i < len; i++) {
        str += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
}

int64_t getMillisec() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
