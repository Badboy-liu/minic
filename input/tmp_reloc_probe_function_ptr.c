int answer() { return 42; }

int (*fn_ptr)() = answer;

int main() {
    return fn_ptr();
}
