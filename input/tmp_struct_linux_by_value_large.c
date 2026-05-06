struct Quad {
    int a;
    int b;
    int c;
    int d;
    int e;
} g_quad_type_anchor;

int sum_quad(struct Quad quad) {
    return quad.a + quad.b + quad.c + quad.d + quad.e;
}

int main() {
    struct Quad quad = {8, 9, 10, 7, 8};
    return sum_quad(quad);
}
