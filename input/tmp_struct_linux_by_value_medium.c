struct Triple {
    int first;
    int second;
    int third;
} g_triple_type_anchor;

int sum_triple(struct Triple triple) {
    return triple.first + triple.second + triple.third;
}

int main() {
    struct Triple triple = {19, 20, 3};
    return sum_triple(triple);
}
