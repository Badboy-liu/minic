int main() {
    unsigned int x;
    x = 2147483647;
    x = x + 1;
    if (x < 0) {
        return 0;
    }
    if (x > 0) {
        return 42;
    }
    return 0;
}
