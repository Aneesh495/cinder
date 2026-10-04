int main(void) {
    return sizeof(int (*[3])(double)) != 24 || sizeof(int (*)[4]) != 8;
}
