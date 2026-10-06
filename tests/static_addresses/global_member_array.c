struct Data { int x; char text[4]; }; struct Data value; char *p=&value.text[2]; int main(void) { value.text[2]=79; return *p; }
