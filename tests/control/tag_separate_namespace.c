typedef long Entry;
struct Entry { int value; };
int main(void) { return sizeof(Entry) != 8 || sizeof(struct Entry) != 4; }
