int id(int x) {
    return x;
}

int main() {
    int values[3];
    values[0] = 5;
    values[1] = 6;
    values[2] = 7;
    int *p = &values[0];
    return *(p + id(2));
}
