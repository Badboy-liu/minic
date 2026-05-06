extern int data_rel32_target;

int *p = &data_rel32_target;

int main() {
    return *p;
}
