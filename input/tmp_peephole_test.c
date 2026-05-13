int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int main() {
    int x = add(10, 20);
    int y = sub(x, 5);
    if (y > 15) {
        return add(y, 1);
    }
    return 0;
}
