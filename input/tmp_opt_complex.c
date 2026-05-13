int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}
int main() {
    int x = factorial(5);
    if (x > 100) {
        return 1;
    }
    return 0;
}
