// 字符常量类型测试 - 'a' 应为 int 类型
int main() {
    // sizeof('a') 在 C 中应为 sizeof(int) = 4
    int sz = sizeof('a');
    // char 赋值应隐式截断
    char c = 'A';  // 65
    return c - 65;  // 0
}
