// 测试单语句内联 + inline 关键字
inline int square(int x) {
    return x * x;
}

inline int add(int a, int b) {
    return a + b;
}

inline int mul_add(int a, int b, int c) {
    return a * b + c;
}

int main() {
    int a = square(5);       // 25
    int b = add(3, 4);       // 7
    int c = mul_add(2, 3, 4); // 10
    return a + b + c;        // 25 + 7 + 10 = 42
}
