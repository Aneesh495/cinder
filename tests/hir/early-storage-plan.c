struct box { int value; };
int main(void) {
    int initial = 17;
    struct box object = (struct box){initial};
    return object.value;
}
