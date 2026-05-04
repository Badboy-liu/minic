int main() {
    struct Pair {
        int left;
        int right;
    } pair;
    struct Pair *ptr;

    ptr = &pair;
    ptr->left = 19;
    ptr->right = 23;
    return pair.left + pair.right;
}
