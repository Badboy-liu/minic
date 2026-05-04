struct Triple {
    int first;
    int second;
    int third;
} g_triple_type_anchor;

struct Triple make_triple() {
    struct Triple triple = {19, 20, 3};
    return triple;
}

int main() {
    struct Triple triple;
    triple = make_triple();
    return triple.first + triple.second + triple.third;
}
