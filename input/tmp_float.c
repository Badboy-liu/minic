// 测试浮点类型支持
// 预期退出码：42

double add(double a, double b) {
    return a + b;
}

double sub(double a, double b) {
    return a - b;
}

double mul(double a, double b) {
    return a * b;
}

double divide(double a, double b) {
    return a / b;
}

int double_to_int(double x) {
    return (int)x;
}

double int_to_double(int x) {
    return (double)x;
}

int main() {
    // 测试浮点常量
    double a = 3.14;
    double b = 2.0;

    // 测试浮点算术
    double c = add(a, b);      // 5.14
    double d = sub(a, b);      // 1.14
    double e = mul(b, b);      // 4.0
    double f = divide(a, b);   // 1.57

    // 测试浮点比较
    if (a > b) {
        // a (3.14) > b (2.0)，成立
    } else {
        return 1;
    }

    // 测试浮点到整数转换
    int int_val = double_to_int(3.7);  // 3
    if (int_val != 3) {
        return 2;
    }

    // 测试整数到浮点转换
    double dbl_val = int_to_double(42);
    if (dbl_val < 41.9 || dbl_val > 42.1) {
        return 3;
    }

    // 测试浮点赋值和返回
    double result = dbl_val;
    return (int)result;  // 42
}
