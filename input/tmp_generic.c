// 测试 _Generic 泛型选择
int int_fn(void) { return 10; }
double double_fn(void) { return 20.0; }
int default_fn(void) { return 30; }

int main(void) {
    int x = 0;
    // x 是 int 类型，应选择 int_fn
    int result = _Generic(x, int: int_fn, double: double_fn, default: default_fn)();
    if (result != 10) return 1;
    return 42;
}
