int main() {
    int x = 0;
    int y = 0;

    // 前缀自增
    y = ++x;
    if (x != 1) return 1;
    if (y != 1) return 2;

    // 后缀自增
    x = 5;
    y = x++;
    if (x != 6) return 3;
    if (y != 5) return 4;

    // 前缀自减
    x = 10;
    y = --x;
    if (x != 9) return 5;
    if (y != 9) return 6;

    // 后缀自减
    x = 10;
    y = x--;
    if (x != 9) return 7;
    if (y != 10) return 8;

    // 在循环中使用
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += i;
    }
    if (sum != 45) return 9;

    return 0;
}
