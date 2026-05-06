int main() {
    int x = 5;
    int result = 0;

    if (x > 3) {
        goto skip;
        result = 99;
    }

skip:
    result = result + 10;

    goto end;
    result = result + 100;

end:
    result = result + 32;
    return result;
}
