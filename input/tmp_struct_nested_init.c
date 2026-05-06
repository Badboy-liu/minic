struct Pair {
    int left;
    int right;
} g_pair_type_anchor;

struct Wrapper {
    struct Pair inner;
    int value;
} g_wrapper = {{10, 11}, 21};

int main() {
    return g_wrapper.inner.left + g_wrapper.inner.right + g_wrapper.value;
}
