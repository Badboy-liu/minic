int add(int a, int b) {
    return a + b;
}

int mulsum(int limit, int factor) {
    int total = 0;
    int i = 0;
    for (i = 1; i <= limit; i = i + 1) {
        if (i == 3) {
            continue;
        }
        total = add(total, i * factor);
        if (total > 20) {
            break;
        }
    }
    return total;
}

int main() {
    return mulsum(5, 2);
}
