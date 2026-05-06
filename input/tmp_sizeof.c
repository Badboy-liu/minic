int main() {
    // sizeof 基本类型
    if (sizeof(char) != 1) return 1;
    if (sizeof(short) != 2) return 2;
    if (sizeof(int) != 4) return 3;
    if (sizeof(long) != 8) return 4;
    if (sizeof(long long) != 8) return 5;
    if (sizeof(void *) != 8) return 6;

    // sizeof 表达式
    int x = 42;
    if (sizeof(x) != 4) return 7;
    if (sizeof(x + 1) != 4) return 8;

    // sizeof 指针
    int *p = &x;
    if (sizeof(p) != 8) return 9;

    // sizeof 数组
    int arr[10];
    if (sizeof(arr) != 40) return 10;

    return 0;
}
