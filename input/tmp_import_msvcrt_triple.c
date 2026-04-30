extern int puts(char *text);
extern int putchar(int ch);
extern int printf(char *fmt, int value);

int main() {
    puts("triple");
    putchar(33);
    printf(" n=%d\n", 7);
    return 1;
}
