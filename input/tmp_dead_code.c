int main() {
    int x = 5;
    if (0) {
        x = 999;  // 不可达，应被消除
    }
    if (1) {
        x = 42;
    }
    while (0) {
        x = 999;  // 不可达，应被消除
    }
    return x;  // 应返回 42
}
