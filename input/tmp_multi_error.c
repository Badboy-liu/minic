// 测试解析器多错误报告
int main() {
    int x = ;        // 错误1：缺少表达式
    int y = 2;
    int z = +;       // 错误2：缺少表达式
    return y;
}
