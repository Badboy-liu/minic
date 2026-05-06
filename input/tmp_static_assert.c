// 测试 _Static_assert
_Static_assert(sizeof(int) == 4, "int must be 4 bytes");
_Static_assert(sizeof(long long) == 8, "long long must be 8 bytes");
_Static_assert(1 == 1, "basic assertion");

int main(void) {
    _Static_assert(sizeof(char) == 1, "char must be 1 byte");
    return 42;
}
