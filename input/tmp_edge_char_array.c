// 测试字符数组操作
// 预期退出码：42

int main() {
    char str[6] = "hello";
    int len = 0;

    // 计算字符串长度
    while (str[len] != 0) {
        len = len + 1;
    }
    // len = 5

    // 修改字符
    str[0] = 'H';
    int first = str[0];  // 'H' = 72

    return first + len - 35;  // 72 + 5 - 35 = 42
}
