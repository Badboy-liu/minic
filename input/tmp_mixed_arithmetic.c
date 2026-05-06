// 测试混合 int/float 隐式转换
// 预期退出码：42

int main() {
    // 混合 int/float 算术
    double a = 1 + 2.0;          // int + double -> double, 结果 3.0
    double b = 10.0 - 3;         // double - int -> double, 结果 7.0
    double c = 2 * 2.5;          // int * double -> double, 结果 5.0
    double d = 20.0 / 4;         // double / int -> double, 结果 5.0

    // 混合 int/float 比较
    int cmp1 = (3.14 > 2);       // double > int -> 1
    int cmp2 = (1 < 2.5);        // int < double -> 1
    int cmp3 = (3.0 >= 3);       // double >= int -> 1

    // int -> float 赋值
    float f = 42;                // int -> float
    int fromFloat = (int)f;      // float -> int

    // 混合三元运算符
    double ternary = 1 ? 2.0 : 3;  // int : double -> double

    int result = (int)a + (int)b + (int)c + (int)d + cmp1 + cmp2 + cmp3;
    // 3 + 7 + 5 + 5 + 1 + 1 + 1 = 23

    // 加上 fromFloat 差值
    result = result + (fromFloat - 23);  // 23 + (42 - 23) = 42

    return result;
}
