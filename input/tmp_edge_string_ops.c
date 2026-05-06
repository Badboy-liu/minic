// 测试字符串操作
int main() {
    char msg[6] = "hello";
    char *p = msg;
    int len = 0;
    while (*p != 0) {
        len = len + 1;
        p = p + 1;
    }
    return len;  // 5
}
