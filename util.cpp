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

// void stringGenerator(std::string& str, int len)
// {
//     int rand;
//     std::random_device dev;
//     std::mt19937 rng(dev());
//     std::uniform_int_distribution<std::mt19937::result_type> gen(0,255); // distribution in range [0, 255]

//     str.reserve(len);
//     for(int i = 0; i < len; i++) {
//         rand = gen(rng);
//         str += static_cast<std::string>(rand);
//     }

// }
