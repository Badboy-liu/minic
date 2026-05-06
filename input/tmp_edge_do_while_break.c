// 测试 do-while 循环中 break 的边界情况
// 预期退出码：42

int main() {
    int sum = 0;
    int i = 0;
    do {
        i = i + 1;
        if (i > 6) break;
        sum = sum + i;
    } while (1);
    // sum = 1+2+3+4+5+6 = 21

    int product = 1;
    int j = 1;
    do {
        product = product * j;
        j = j + 1;
    } while (j <= 4);
    // product = 1*2*3*4 = 24... 不对
    // product = 1*1*2*3*4 = 24... 还是不对
    // j 从 1 开始，先乘 j，再 j++
    // 第1轮: product=1*1=1, j=2
    // 第2轮: product=1*2=2, j=3
    // 第3轮: product=2*3=6, j=4
    // 第4轮: product=6*4=24, j=5
    // product = 24... 但 21 + 24 = 45，需要 42

    return sum + product - 3;  // 21 + 24 - 3 = 42
}
