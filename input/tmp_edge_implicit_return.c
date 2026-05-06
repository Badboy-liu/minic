// 测试非 void 函数没有显式 return（行为未定义，但应该不崩溃）
// 编译成功即可，退出码不可预测
int get_value() {
    int x = 42;
}
int main() {
    get_value();
    return 0;
}
