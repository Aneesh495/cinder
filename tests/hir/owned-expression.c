static int identity(int value) { return value; }
int main(void) {
    int value = 17;
    int *pointer = &value;
    return (int)identity(*pointer);
}
