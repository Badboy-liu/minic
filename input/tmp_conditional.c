// 测试预处理器条件编译
// 预期退出码：42

#define LEVEL 2
#define FEATURE_A 1
// FEATURE_B 未定义

int main() {
    int result = 0;

    // #if 基本数字常量
#if 1
    result = result + 1;
#endif

    // #if 宏值
#if LEVEL == 2
    result = result + 2;
#endif

    // #ifdef 已定义的宏
#ifdef FEATURE_A
    result = result + 4;
#endif

    // #ifdef 未定义的宏
#ifdef FEATURE_B
    result = result + 100;  // 不应执行
#endif

    // #ifndef 未定义的宏
#ifndef FEATURE_B
    result = result + 8;
#endif

    // #if defined() + &&
#if defined(FEATURE_A) && defined(LEVEL)
    result = result + 16;
#endif

    // #if defined() + ||，其中一个未定义
#if defined(FEATURE_A) || defined(FEATURE_B)
    result = result + 10;
#endif

    // 1+2+4+8+16+10 = 41，再加 1
    result = result + 1;

    return result;  // 42
}
