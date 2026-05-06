// 测试复杂预处理器条件表达式
// 预期退出码：42

#define VERSION 3
#define PLATFORM_WINDOWS 1
#define PLATFORM_LINUX 0
#define DEBUG 0
#define FEATURE_X 1
#define FEATURE_Y 0

int main() {
    int result = 0;

    // 算术运算
#if VERSION + 1 == 4
    result = result + 1;
#endif

    // 逻辑与
#if PLATFORM_WINDOWS && FEATURE_X
    result = result + 2;
#endif

    // 逻辑或
#if FEATURE_X || FEATURE_Y
    result = result + 4;
#endif

    // 逻辑非
#if !DEBUG
    result = result + 8;
#endif

    // 复杂组合
#if (VERSION > 2) && (PLATFORM_WINDOWS || PLATFORM_LINUX) && !DEBUG
    result = result + 16;
#endif

    // elif 分支
#if DEBUG
    result = result + 100;
#elif VERSION == 3
    result = result + 10;
#else
    result = result + 200;
#endif

    // 嵌套条件
#if PLATFORM_WINDOWS
    #if FEATURE_X
        result = result + 1;
    #endif
#endif

    // 1+2+4+8+16+10+1 = 42
    return result;
}
