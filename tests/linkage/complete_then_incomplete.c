char text[4]="abc"; extern char text[]; int main(void) { return sizeof(text); }
