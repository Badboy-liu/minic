// 测试 while 循环中 break 的边界情况
// 预期退出码：15

int main() {
    int sum = 0;
    int i = 1;
    while (1) {
        if (i > 5) break;
        sum = sum + i;
        i = i + 1;
    }
    return sum;  // 1+2+3+4+5 = 15
}
