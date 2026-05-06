// 测试数组访问和指针算术
int main() {
    int arr[5];
    int i = 0;
    while (i < 5) {
        arr[i] = i * 10;
        i = i + 1;
    }
    int *p = arr;
    return *(p + 2) + arr[3];  // 20 + 30 = 50
}
