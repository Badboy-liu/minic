// 测试嵌套 for 循环
// 预期退出码：30

int main() {
    int sum = 0;
    for (int i = 0; i < 5; i = i + 1) {
        for (int j = 0; j < 6; j = j + 1) {
            sum = sum + 1;
        }
    }
    return sum;  // 5 * 6 = 30
}
