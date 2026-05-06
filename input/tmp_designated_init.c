int main() {
    struct Point {
        int x;
        int y;
        int z;
    } p = {.y = 2, .x = 1, .z = 3};

    return p.x + p.y + p.z;
}
