// 测试 const 限定符
int main() {
    // 测试 const 变量声明
    const int x = 42;
    if (x != 42) {
        return 1;
    }

    // 测试 volatile 变量声明
    volatile int y = 10;
    if (y != 10) {
        return 2;
    }

    // 测试 const 和 volatile 组合
    const volatile int z = 99;
    if (z != 99) {
        return 3;
    }

    return 42;
}
