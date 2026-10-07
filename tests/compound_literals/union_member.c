union U { long word; double fraction; }; int main(void) { union U *p=&(union U){.fraction=2.75}; return p->fraction != 2.75; }
