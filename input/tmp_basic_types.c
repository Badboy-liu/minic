char one() {
    char x = 1;
    return x;
}

short two() {
    short x = 2;
    return x;
}

long three() {
    long x = 3;
    return x;
}

long long four() {
    long long x = 4;
    return x;
}

int main() {
    char bytes[2];
    short words[2];
    bytes[0] = one();
    bytes[1] = 5;
    words[0] = two();
    words[1] = 6;
    return bytes[0] + bytes[1] + words[0] + words[1] + three() + four();
}
