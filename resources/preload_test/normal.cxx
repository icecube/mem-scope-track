#include <iostream>

#include "test.h"

int main() {
    mem::scope = "blah";
    mem::test();

    std::cout << "scope:" << mem::scope << std::endl;

    return 0;
}
