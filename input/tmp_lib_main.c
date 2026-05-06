// 静态库测试 - 主程序
int add(int a, int b);
int mul(int a, int b);

int main() {
    int a = add(3, 4);   // 7
    int b = mul(5, 6);   // 30
    return a + b;         // 37
}
