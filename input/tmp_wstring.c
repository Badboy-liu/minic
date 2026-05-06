int puts(const char *s);
int printf(const char *fmt, ...);

int main() {
    // 宽字符串字面量（语法支持，值等同普通字符串）
    const char *w = L"hello";
    puts(w);

    // UTF-8 字符串
    const char *u8s = u8"utf8 string";
    puts(u8s);

    // UTF-16/32 字符串
    const char *u16s = u"utf16";
    const char *u32s = U"utf32";
    puts(u16s);
    puts(u32s);

    // 宽字符字面量
    int wc = L'A';
    if (wc != 65) return 1;

    // 宽字符串拼接（宽 + 窄）
    const char *concat = L"wide" "normal";
    if (concat[0] != 'w') return 2;

    return 42;
}
