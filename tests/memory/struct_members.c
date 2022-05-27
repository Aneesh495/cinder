struct S { char tag; int value; float weight; }; int main(void) { struct S s; s.tag = 7; s.value = 25; s.weight = 1.5f; return s.tag + s.value + (int)(s.weight * 4.0f); }
