typedef int MyInt;
typedef int* IntPtr;
typedef long long BigInt;

int main() {
    MyInt x = 42;
    if (x != 42) return 1;

    IntPtr p = &x;
    if (*p != 42) return 2;

    BigInt big = 1000000000;
    if (big != 1000000000) return 3;

    return 0;
}
