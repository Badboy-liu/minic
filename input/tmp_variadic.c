// 测试可变参数函数：va_start 和 va_arg
// 预期退出码：42

int my_sum(int count, ...) {
    char* ap;
    __builtin_va_start(ap, count);
    int result = 0;
    int i = 0;
    while (i < count) {
        result = result + __builtin_va_arg(ap, int);
        i = i + 1;
    }
    __builtin_va_end(ap);
    return result;
}

int main() {
    // my_sum(3, 10, 20, 12) = 10 + 20 + 12 = 42
    return my_sum(3, 10, 20, 12);
}
