int main() {
    int i = 0;
    int sum = 0;
    while (1) {
        i = i + 1;
        if (i == 2 || i == 4) {
            continue;
        }
        sum = sum + i;
        if (!(sum < 8) && i >= 3) {
            break;
        }
    }
    return sum;
}
