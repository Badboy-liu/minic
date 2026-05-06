extern void *malloc(int size);
extern void free(void *ptr);

int main() {
    void *p = malloc(4);
    free(p);
    return 42;
}
