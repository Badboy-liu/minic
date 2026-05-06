// 测试 C11/现代关键字的词法识别和解析
// 预期退出码：42

// restrict 限定符用于函数参数
void use_restrict(int *restrict p) {
    *p = 42;
}

// inline 函数
inline int add_inline(int a, int b) {
    return a + b;
}

// _Atomic 变量
_Atomic int atomic_counter = 0;

// _Thread_local 变量
_Thread_local int tls_var = 0;

// _Alignas 变量
_Alignas(16) int aligned_var = 0;

int main(void) {
    // 测试 restrict 参数
    int x = 0;
    use_restrict(&x);
    if (x != 42) {
        return 1;
    }

    // 测试 inline 函数调用
    int y = add_inline(20, 22);
    if (y != 42) {
        return 2;
    }

    return 42;
}
