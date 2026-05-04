struct Pair {
    int left;
    int right;
} g_pair_type_anchor;

struct Pair make_pair() {
    struct Pair pair = {19, 23};
    return pair;
}

int sum_pair(struct Pair pair) {
    return pair.left + pair.right;
}

int main() {
    return sum_pair(make_pair());
}
