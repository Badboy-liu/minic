// 测试 const 变量不可赋值
int main() {
    const int x = 42;
    x = 100;  // 应该报错：cannot assign to const variable
    return 0;
}
