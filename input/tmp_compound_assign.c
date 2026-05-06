int main() {
    int x = 100;

    x += 5;
    if (x != 105) return 1;

    x -= 3;
    if (x != 102) return 2;

    x *= 2;
    if (x != 204) return 3;

    x /= 4;
    if (x != 51) return 4;

    x %= 8;
    if (x != 3) return 5;

    x = 1;
    x <<= 5;
    if (x != 32) return 6;

    x >>= 2;
    if (x != 8) return 7;

    x &= 15;
    if (x != 8) return 8;

    x ^= 255;
    if (x != 247) return 9;

    x |= 256;
    if (x != 503) return 10;

    return 0;
}
