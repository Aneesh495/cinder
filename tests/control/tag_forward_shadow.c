struct Entry { char value; };
int main(void) {
    int size = (int)sizeof(struct Entry);
    { struct Entry; struct Entry *pointer; struct Entry { long value; }; size += (int)sizeof(*pointer); }
    return size != 9;
}
