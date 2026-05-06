// 测试 unsigned 的 movzx 修正
int main() {
    // 测试 unsigned char 使用 movzx
    unsigned char uc = 255;
    if (uc != 255) {
        return 1;
    }

    // 测试 unsigned short 使用 movzx
    unsigned short us = 65535;
    if (us != 65535) {
        return 2;
    }

    // 测试 signed char 使用 movsx（char 默认有符号）
    char sc = -1;
    if (sc != -1) {
        return 3;
    }

    // 测试 signed short 使用 movsx（short 默认有符号）
    short ss = -1;
    if (ss != -1) {
        return 4;
    }

    // 测试 unsigned char 与 char 的区别
    // unsigned char 200 应该是 200
    unsigned char auc = 200;
    if (auc != 200) {
        return 5;
    }

    return 42;
}
