int main() {
    int x = 10;
    int y = 20;
    int sum = 0;
    int i = 0;
    while (i < 100) {
        int invariant = x + y;
        sum = sum + invariant;
        i = i + 1;
    }
    return sum;
}
