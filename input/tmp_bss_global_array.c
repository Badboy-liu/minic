char bytes[4];

int main() {
    if (bytes[0] != 0) {
        return 1;
    }
    if (bytes[3] != 0) {
        return 2;
    }
    bytes[0] = 10;
    bytes[3] = 25;
    return bytes[0] + bytes[3];
}
