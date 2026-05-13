int compute(int x) {
    int a = x * 8;
    int b = x / 4;
    int c = x % 16;
    return a + b + c;
}

int main() {
    return compute(100);
}
