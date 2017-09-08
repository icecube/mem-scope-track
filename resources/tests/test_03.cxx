#include <memory>
#include "test.h"

int main() {
    memory::set_scope("main");

    auto test = new int;
    *test = 5;

    return 0;
}