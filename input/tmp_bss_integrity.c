int counter;
char bytes[4];

int main() {
    if (counter != 0) {
        return 1;
    }
    if (bytes[0] != 0) {
        return 2;
    }
    if (bytes[3] != 0) {
        return 3;
    }

    counter = 7;
    bytes[0] = 10;
    bytes[3] = 25;

    return counter + bytes[0] + bytes[3];
}
