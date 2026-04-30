float addf(float a, float b) {
    return a + b;
}

int main() {
    float x;
    x = addf(19.5, 22.5);
    if (x == 42.0) {
        return 42;
    }
    return 0;
}
