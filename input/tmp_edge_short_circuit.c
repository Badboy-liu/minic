// 测试短路求值
// 预期退出码：42

int side_effect(int val, int *counter) {
    *counter = *counter + 1;
    return val;
}

int main() {
    int count = 0;
    int a = 1;
    int b = 0;

    // && 短路：b 为 0，不调用 side_effect
    int result1 = (b != 0) && (side_effect(1, &count) != 0);
    // count 应该还是 0

    // || 短路：a 为 1，不调用 side_effect
    int result2 = (a != 0) || (side_effect(1, &count) != 0);
    // count 应该还是 0

    // 不短路：两个都会执行
    int result3 = (a != 0) && (side_effect(1, &count) != 0);
    // count 应该是 1

    int result4 = (b != 0) || (side_effect(1, &count) != 0);
    // count 应该是 2

    return count + 40;  // 2 + 40 = 42
}
