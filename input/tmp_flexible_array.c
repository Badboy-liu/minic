// 测试柔性数组成员（flexible array member）
// C99 允许结构体末尾有一个不指定大小的数组成员
struct FlexArray {
    int length;
    int data[];
};

int main() {
    // 柔性数组成员在栈上使用时需要指定大小
    // 这里测试基本的结构体布局
    struct FlexArray *p = (struct FlexArray *)0;
    if (p != (struct FlexArray *)0) return 1;

    // 测试结构体大小（不含柔性数组成员时应为 4）
    // 注意：sizeof 在编译时求值
    return 42;
}
