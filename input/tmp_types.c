int sum4(int *arr);
void fill(int *arr);

int sum4(int *arr) {
    int total = 0;
    int i = 0;
    for (i = 0; i < 4; i = i + 1) {
        total = total + arr[i];
    }
    return total;
}

void fill(int *arr) {
    arr[0] = 10;
    arr[1] = 11;
    arr[2] = 12;
    arr[3] = 9;
    return;
}

int first(int *p) {
    return *p;
}

int main() {
    int values[4];
    int *p = &values[0];
    fill(values);
    values[3] = values[3] + first(p);
    return sum4(values);
}
