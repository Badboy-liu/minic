enum Color { RED, GREEN, BLUE };
enum Direction { NORTH = 1, SOUTH = 2, EAST = 4, WEST = 8 };

int main() {
    if (RED != 0) return 1;
    if (GREEN != 1) return 2;
    if (BLUE != 2) return 3;

    if (NORTH != 1) return 4;
    if (SOUTH != 2) return 5;
    if (EAST != 4) return 6;
    if (WEST != 8) return 7;

    enum Color c = GREEN;
    if (c != 1) return 8;

    // 枚举在 switch 中使用
    int result = 0;
    switch (c) {
        case RED: { result = 10; break; }
        case GREEN: { result = 20; break; }
        case BLUE: { result = 30; break; }
    }
    if (result != 20) return 9;

    return 0;
}
