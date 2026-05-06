// 测试 _Static_assert 无消息版本（C23 改进）
_Static_assert(sizeof(int) == 4);
_Static_assert(1);

int main(void) {
    _Static_assert(sizeof(char) == 1);
    return 42;
}
