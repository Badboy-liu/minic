// 测试深度嵌套的控制流
int main() {
    int result = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 3) {
            if (i == 1 && j == 1) {
                result = result + 100;
            } else {
                result = result + 1;
            }
            j = j + 1;
        }
        i = i + 1;
    }
    return result;  // 8*1 + 1*100 = 108
}
