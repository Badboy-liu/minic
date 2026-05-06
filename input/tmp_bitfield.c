int main() {
    struct Flags {
        int x : 3;
        int y : 5;
        int z : 8;
    } f;

    f.x = 5;
    f.y = 13;
    f.z = 42;

    return f.x + f.y + f.z;
}
