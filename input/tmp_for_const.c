int main() {
    int x = 0;
    for (; 1; ) {
        x = x + 1;
        if (x >= 10) break;
    }
    return x;
}
