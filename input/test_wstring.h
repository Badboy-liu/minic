// 宽字符/多字节字符串字面量测试
int puts(const char *s);
int printf(const char *fmt, ...);

int test_wstring() {
    // 基本宽字符串
    const char *w = L"hello";
    puts(w);

    // UTF-8 字符串
    const char *u8s = u8"utf8 string";
    puts(u8s);

    // UTF-16 字符串
    const char *u16s = u"utf16 string";
    puts(u16s);

    // UTF-32 字符串
    const char *u32s = U"utf32 string";
    puts(u32s);

    // 宽字符串拼接
    const char *concat = L"wide" " normal";
    puts(concat);

    // 宽字符字面量
    int wc = L'A';
    printf("L'A' = %d\n", wc);

    return 0;
}
