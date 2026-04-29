int forty(void) {
    return 40;
}

int two(void) {
    return 2;
}

int main() {
    int (*table[2])(void) = { forty, two };
    return table[0]() + table[1]();
}
