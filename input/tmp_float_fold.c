// 测试浮点常量折叠
// 预期退出码：42

int main() {
    // 浮点常量折叠：编译时计算
    double a = 1.0 + 2.0;       // 应折叠为 3.0
    double b = 10.0 - 3.0;      // 应折叠为 7.0
    double c = 2.5 * 4.0;       // 应折叠为 10.0
    double d = 20.0 / 5.0;      // 应折叠为 4.0

    // 浮点比较折叠
    int cmp = (3.14 > 2.0);     // 应折叠为 1

    int result = (int)a + (int)b + (int)c + (int)d + cmp;
    // 3 + 7 + 10 + 4 + 1 = 25

    // 加上整数
    result = result + 17;        // 25 + 17 = 42

    return result;
}
