// 测试循环不变量外提
int main() {
    int x = 10;
    int y = 20;
    int sum = 0;
    int i = 0;
    // x + y 是循环不变量，应被外提
    while (i < 5) {
        int z = x + y;
        sum = sum + z;
        i = i + 1;
    }
    // sum = 5 * (10 + 20) = 150
    return sum;
}
