static int called;
static int unused(void) { ++called; return 100; }
int main(void) {
    int value = 3;
    unsigned long bytes = sizeof(value++);
    int selected = _Generic(value, int: 9, default: unused());
    return value + (int)bytes + selected + called;
}
