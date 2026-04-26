void noop(void);
void set_first(int *p, int value);

void noop(void) {
    return;
}

void set_first(int *p, int value) {
    *p = value;
}

int main() {
    int x = 41;
    int *p = &x;
    noop();
    set_first(p, x + 1);
    return *p;
}
