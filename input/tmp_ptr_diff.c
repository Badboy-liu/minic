// 测试指针减法
// 预期退出码：42

int main() {
    int arr[10];
    int *p1 = &arr[0];
    int *p2 = &arr[5];
    int *p3 = &arr[9];

    // 指针减法：元素个数
    long long diff1 = p2 - p1;  // 5
    long long diff2 = p3 - p1;  // 9

    // 验证
    if (diff1 != 5) return 1;
    if (diff2 != 9) return 2;

    // char 指针
    char buf[100];
    char *c1 = &buf[0];
    char *c2 = &buf[42];
    long long cdiff = c2 - c1;  // 42
    if (cdiff != 42) return 3;

    // 反向减法：用加法验证
    long long diff3 = p3 - p2;  // 4
    if (diff3 + diff1 != 9) return 4;  // 4 + 5 = 9

    return 42;
}
