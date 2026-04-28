int one() {
    return 1;
}

int two() {
    return 2;
}

int (*fn_table[2])() = { one, two };

int main() {
    return fn_table[1]();
}
