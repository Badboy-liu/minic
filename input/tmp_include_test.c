// 测试 #include 功能
// 使用 -I 选项指定搜索路径
// 预期退出码：42

#include <test_header.h>

int main() {
    int x = HEADER_CONSTANT;
    if (x != 100) return 1;

    int y = HEADER_ADD(20, 22);
    if (y != 42) return 2;

    return 42;
}
