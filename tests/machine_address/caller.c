extern int external_value;
int external_target(int value);
int main(void) {
    int *pointer = &external_value;
    int (*function)(int) = external_target;
    return function(*pointer);
}
