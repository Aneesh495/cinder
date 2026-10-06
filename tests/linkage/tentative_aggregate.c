struct Data { int x; }; struct Data value; extern struct Data value; int main(void) { value.x=79; return value.x; }
