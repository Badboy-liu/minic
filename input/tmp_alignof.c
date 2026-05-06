// 测试 _Alignof 运算符
int main(void) {
    if (_Alignof(int) != 4) return 1;
    if (_Alignof(char) != 1) return 2;
    if (_Alignof(long long) != 8) return 3;
    if (_Alignof(void *) != 8) return 4;

    // _Alignof 作为表达式的一部分
    int a = _Alignof(int);
    if (a != 4) return 5;

    return 42;
}
