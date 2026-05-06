#include "tmp_include_test.h"

#define SQUARE(x) ((x) * (x))

int main() {
    int x = HELLO_VALUE;
    if (x != 42) return 1;

    int y = ADD(3, 4);
    if (y != 7) return 2;

    int z = MAX(10, 20);
    if (z != 20) return 3;

    int w = SQUARE(5);
    if (w != 25) return 4;

    return 0;
}
