int int_fn(void) { return 10; }
int default_fn(void) { return 30; }

int main(void) {
    int x = 0;
    int result = _Generic(x, int: int_fn, default: default_fn)();
    return result;
}
