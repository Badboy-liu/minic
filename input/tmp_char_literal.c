int main() {
    int a = 'a';
    if (a != 97) return 1;

    int b = 'Z';
    if (b != 90) return 2;

    int c = '0';
    if (c != 48) return 3;

    // 转义字符
    int n = '\n';
    if (n != 10) return 4;

    int t = '\t';
    if (t != 9) return 5;

    int r = '\r';
    if (r != 13) return 6;

    int bs = '\\';
    if (bs != 92) return 7;

    int sq = '\'';
    if (sq != 39) return 8;

    int z = '\0';
    if (z != 0) return 9;

    return 42;
}
