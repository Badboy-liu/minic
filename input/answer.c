int add(int a, int b) {
    return a + b;
}

int accumulate(int limit, int step) {
    int total = 0;
    int i = 0;
    for (i = 0; i < limit; i = i + 1) {
        total = add(total, step);
    }
    return total;
}

int main() {
    return accumulate(6, 7);
}
