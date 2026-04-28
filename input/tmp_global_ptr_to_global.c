int x;
int *p = &x;

int main() {
    *p = 42;
    return x;
}
