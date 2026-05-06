int square(int x) {
    return x * x;
}

int add(int a, int b) {
    return a + b;
}

int main() {
    int a = square(5);
    int b = add(3, 4);
    return a + b;  // 25 + 7 = 32
}
