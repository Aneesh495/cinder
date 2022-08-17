int f(void) { static char s[]="abc"; ++s[1]; return s[1]; } int main(void) { return f()!=99 || f()!=100; }
