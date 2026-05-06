// 测试 #line 指令和 __LINE__/__FILE__ 预定义宏
// 预期退出码：42

#line 100 "check_file.c"

int get_line(void) {
    return __LINE__;
}

int main(void) {
    // 测试 __LINE__（get_line 在 #line 100 后的第 102 行）
    int line = get_line();
    if (line != 102) {
        return 1;
    }

    // 测试 __FILE__（由 #line 设置为 "check_file.c"）
    const char *file = __FILE__;
    if (file[0] != 'c') {
        return 2;
    }
    if (file[5] != '_') {
        return 3;
    }

    return 42;
}
