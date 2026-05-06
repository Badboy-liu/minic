// 测试预处理器 ##（拼接）运算符
// 预期退出码：42

#define CONCAT(a, b) a ## b

int main() {
    int xy = 42;
    int val = CONCAT(x, y);
    return val;
}
