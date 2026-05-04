struct Pair {
    int left;
    int right;
} g_pair_type_anchor;

int sum_pair(struct Pair pair) {
    return pair.left + pair.right;
}

int main() {
    struct Pair pair = {19, 23};
    return sum_pair(pair);
}
