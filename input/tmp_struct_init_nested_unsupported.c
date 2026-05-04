struct Pair {
    int left;
    int right;
} g_pair_type_anchor;

struct Wrapper {
    struct Pair inner;
    int value;
} g_wrapper = {{1, 2}, 3};

int main() {
    return 0;
}
