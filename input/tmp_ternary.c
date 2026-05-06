int main() {
    int x = 10;
    int y = 20;

    // 基本三目
    int r = x > y ? x : y;
    if (r != 20) return 1;

    // 另一个分支
    r = x < y ? x : y;
    if (r != 10) return 2;

    // 嵌套三目
    int a = 1;
    int b = 2;
    int c = 3;
    r = a > b ? (a > c ? a : c) : (b > c ? b : c);
    if (r != 3) return 3;

    // 三目在表达式中
    r = (x > 5 ? 100 : 200) + (y > 15 ? 10 : 20);
    if (r != 110) return 4;

    return 0;
}
