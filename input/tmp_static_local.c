// 测试 static 局部变量
int puts(const char *s);

int counter(void) {
    static int count = 0;
    count = count + 1;
    return count;
}

int main(void) {
    int c1 = counter();
    int c2 = counter();
    int c3 = counter();
    // c1=1, c2=2, c3=3 => 返回 1+2+3 = 6，再加上一些验证
    if (c1 != 1) return 1;
    if (c2 != 2) return 2;
    if (c3 != 3) return 3;
    return 42;
}
