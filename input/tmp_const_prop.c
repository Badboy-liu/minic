int main() {
    int x = 10;
    int y = x + 5;  // 应被常量传播后折叠为 15
    int z = 20;
    z = 30;          // z 重新赋值
    int w = z + 1;   // 应被常量传播后折叠为 31
    return y + w;    // 应被折叠为 46
}
