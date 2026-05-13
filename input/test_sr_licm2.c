int compute(int x) {
    int sum = 0;
    for (int i = 0; i < 10; i++) {
        int a = x * 8;
        int b = x / 4;
        int c = x % 16;
        sum = sum + a + b + c;
    }
    return sum;
}

int main() {
    return compute(42);
}
