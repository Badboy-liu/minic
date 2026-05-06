int main() {
    // 基本逗号运算符
    int a = (1, 2, 3);
    if (a != 3) return 1;

    // 逗号运算符结果类型为右侧类型
    int b;
    b = (10, 20);
    if (b != 20) return 2;

    // 逗号运算符在 for 循环更新中
    int i;
    int sum = 0;
    for (i = 0; i < 5; i++) {
        sum = sum + i;
    }
    if (sum != 10) return 3;

    // 逗号运算符在 return 中：结果为右侧值
    int x = 10;
    int y = 32;
    int ret = (x, y);
    if (ret != 32) return 4;

    return 42;
}
