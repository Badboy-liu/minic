int main() {
    int x = 100;
    int a = x * 8;     // 强度削减：x << 3
    int b = x / 4;     // 强度削减：x >> 2
    int c = x % 16;    // 强度削减：x & 15
    int d = x * 1;     // 消除：x
    int e = x / 1;     // 消除：x
    int f = x * 0;     // 消除：0
    return a + b + c + d + e + f;
}
