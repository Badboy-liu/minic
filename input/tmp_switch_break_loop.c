int main() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        switch (i) {
            case 0: { sum = sum + 1; break; }
            case 1: { sum = sum + 2; break; }
            default: { sum = sum + 10; break; }
        }
        i = i + 1;
    }
    return sum;
}
