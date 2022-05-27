struct S { char tag; int value; }; struct S s; int main(void) { s.tag = 7; s.value = 39; return s.tag + s.value; }
