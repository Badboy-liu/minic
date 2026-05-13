int add(int a, int b) { return a + b; }
int main() {
    int x = 10;
    int y = 20;
    int z = add(x, y);
    if (z > 25) { return 1; }
    return 0;
}
