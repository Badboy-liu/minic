int main() {
    int x = 256;
    char c = (char)x;
    int back = (int)c;

    long long big = (long long)x;
    int small = (int)big;

    int *p = (int *)0;
    long long lp = (long long)p;

    return 42;
}
