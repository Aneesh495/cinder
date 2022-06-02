int main(void) {
    unsigned long wide = 256UL;
    _Bool local = wide;
    return local + 2 * (_Bool)(wide * 2UL);
}
