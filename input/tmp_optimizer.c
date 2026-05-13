// 测试优化器功能：复制传播、死存储消除、自运算简化
// 预期退出码：42

int main() {
    // 测试自运算简化：x ^ x → 0
    int a = 42;
    int b = a ^ a;  // 应优化为 0
    if (b != 0) return 1;

    // 测试自运算简化：x | x → x
    int c = a | a;  // 应优化为 a
    if (c != 42) return 2;

    // 测试自运算简化：x & x → x
    int d = a & a;  // 应优化为 a
    if (d != 42) return 3;

    // 测试乘法强度削减：x * 5 → (x << 2) + x
    int e = a * 5;
    if (e != 210) return 4;

    // 测试乘法强度削减：x * 3 → (x << 1) + x
    int f = a * 3;
    if (f != 126) return 5;

    return 42;
}
