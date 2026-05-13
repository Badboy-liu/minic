int main() {
    int x = 1;
    int i = 0;
    while (x) {
        i = i + 1;
        if (i >= 10) x = 0;
    }
    return i;
}
