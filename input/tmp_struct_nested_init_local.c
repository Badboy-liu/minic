struct Pair {
    int left;
    int right;
} g_pair_type_anchor;

struct Wrapper {
    struct Pair inner;
    int value;
} g_wrapper_type_anchor;

int main() {
    struct Wrapper w = {{19, 20}, 3};
    return w.inner.left + w.inner.right + w.value;
}
