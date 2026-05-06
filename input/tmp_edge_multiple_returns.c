// 测试函数中多个 return 语句
int abs_val(int x) {
    if (x < 0) {
        return -x;
    }
    return x;
}
int main() {
    int a = abs_val(-5);
    int b = abs_val(3);
    return a + b;  // 5 + 3 = 8
}
