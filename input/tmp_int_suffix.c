// 整数常量后缀和进制测试
int main() {
    // 十六进制
    int a = 0xFF;       // 255
    int b = 0x10;       // 16
    // 八进制
    int c = 077;        // 63
    // 二进制
    int d = 0b1010;     // 10
    // 后缀
    int e = 100U;
    int f = 100L;
    int g = 100UL;
    int h = 100LL;
    // 组合验证
    return a + b + c + d - 344;  // 255+16+63+10 = 344 -> 0
}
