// 测试循环展开优化：for 循环常数次展开
// 预期退出码：42

int main() {
    int sum = 0;

    // 测试 1: 基本 for 展开 (i=0; i<5; i++)
    for (int i = 0; i < 5; i++) {
        sum = sum + i;
    }
    if (sum != 10) return 1;

    // 测试 2: 不同初始值 (j=1; j<5; j++)
    sum = 0;
    for (int j = 1; j < 5; j++) {
        sum = sum + j;
    }
    if (sum != 10) return 2;

    // 测试 3: 嵌套展开
    int product = 0;
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 2; b++) {
            product = product + 1;
        }
    }
    if (product != 6) return 3;

    // 测试 4: 展开后变量仍可用
    int x = 0;
    for (int k = 0; k < 4; k++) {
        x = x + k * k;
    }
    if (x != 14) return 4;

    // 测试 5: while 循环不受影响
    sum = 0;
    int m = 1;
    while (m < 5) {
        sum = sum + m;
        m = m + 1;
    }
    if (sum != 10) return 5;

    return 42;
}
