enum { Count = 7 };
int main(void) {
    int result = Count;
    { enum { Count = 3 }; result += Count; }
    { int Count = 5; result += Count; }
    return result != 15 || Count != 7;
}
