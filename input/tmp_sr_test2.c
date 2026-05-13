int div4(int x) { return x / 4; }
int rem16(int x) { return x % 16; }
int mul8(int x) { return x * 8; }
int main() {
    int a = div4(100);
    int b = rem16(100);
    int c = mul8(100);
    return a + b + c;
}
