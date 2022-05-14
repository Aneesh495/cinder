typedef int Item;
int main(void) {
    Item result = 2;
    { int Item = 5; result += Item; }
    Item second = 3;
    return result + second != 10;
}
