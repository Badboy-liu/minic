struct Pair {
    int left;
    int right;
} g_pair;

int main() {
    g_pair.left = 19;
    g_pair.right = 23;
    return g_pair.left + g_pair.right;
}
