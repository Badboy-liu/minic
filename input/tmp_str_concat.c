// 字符串字面量拼接测试
int puts(const char *s);

int main() {
    // 相邻字符串字面量应自动拼接
    puts("hello " "world");
    puts("ab" "cd" "ef");
    return 0;
}
