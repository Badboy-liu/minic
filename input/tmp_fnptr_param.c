int call_twice(int (*fn)(int), int value) {
    return fn(value) + fn(1);
}

int add_one(int value) {
    return value + 1;
}

int main() {
    return call_twice(add_one, 39);
}
