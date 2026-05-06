// 测试 union 类型
union Value {
    int i;
    char c;
    long l;
};

int main() {
    // 测试 union 大小应为 max(4, 1, 8) = 8
    union Value v;

    // 测试成员访问（所有成员偏移为 0）
    v.i = 42;
    if (v.i != 42) {
        return 1;
    }

    // 写入不同成员，覆盖同一内存
    v.c = 'a';  // 'a' = 97
    if (v.c != 'a') {
        return 2;
    }

    // 指针访问
    union Value *p = &v;
    p->i = 100;
    if (p->i != 100) {
        return 3;
    }

    // sizeof 测试
    if (sizeof(union Value) != 8) {
        return 4;
    }

    return 42;
}
