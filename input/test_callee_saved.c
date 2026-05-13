int add(int a, int b) { return a + b; }

int foo(int x, int y, int z) {
    int a = x + y;
    int b = add(a, z);
    int c = a + b;
    int d = add(c, x);
    return a + b + c + d;
}

int main() {
    return foo(1, 2, 3);
}
