#include <memory>
#include "test.h"

int main() {
    memory::set_scope("main");

    auto test = new int;
    *test = 5;

    memory::set_scope("two");
    auto test2 = new float[10];
    test2[3] = 1.03;
    
    delete test;

    memory::set_scope("none");
    delete test2;

    return 0;
}