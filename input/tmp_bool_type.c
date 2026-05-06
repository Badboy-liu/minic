// 测试 _Bool 类型
int main() {
    _Bool a = 1;
    _Bool b = 0;
    _Bool c = 42;  // 非零值应为 1

    if (a != 1) return 1;
    if (b != 0) return 2;

    // _Bool 在算术运算中应被提升为 int
    int sum = a + b;
    if (sum != 1) return 3;

    // 条件表达式
    _Bool d = (a && !b);
    if (d != 1) return 4;

    return 42;
}
