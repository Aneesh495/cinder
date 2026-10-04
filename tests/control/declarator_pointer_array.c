int main(void) {
    int *array[3];
    int (*pointer)[3];
    return sizeof(array) != 24 || sizeof(pointer) != 8;
}
