long long max64(long long a, long long b) {
    if (a > b) {
        return a;
    }
    return b;
}

int main() {
    long long value = max64(42, 7);
    return value;
}
