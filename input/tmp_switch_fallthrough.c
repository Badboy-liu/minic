int main() {
    int x = 1;
    int result = 0;

    switch (x) {
        case 1:
            result = result + 10;
        case 2:
            result = result + 2;
            break;
        case 3:
            result = result + 30;
            break;
    }

    // x=1 -> case 1 (fall-through) -> case 2: result = 0+10+2 = 12
    // 再测试 default fall-through
    switch (x) {
        case 99:
            result = 0;
            break;
        default:
            result = result + 30;
    }

    // result = 12 + 30 = 42
    return result;
}
