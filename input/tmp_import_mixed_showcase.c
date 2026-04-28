extern int puts(char *text);
extern int GetCurrentProcessId();

int main() {
    puts("minic mixed import");
    return GetCurrentProcessId() > 0;
}
