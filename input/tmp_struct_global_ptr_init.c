int g_value;

struct Holder {
    int *ptr;
    int bias;
} g_holder = {&g_value, 41};

int main() {
    g_value = 1;
    return *g_holder.ptr + g_holder.bias;
}
