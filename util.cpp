#include <iostream>
#include <unistd.h>
#include <string>
#include <random>

void stringGenerator(std::string& str, int len) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    str.reserve(len);
    for (int i = 0; i < len; i++) {
        str += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
}

