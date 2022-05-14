enum { Safe = 1 ? 9 : 1 / 0, Short = 0 && (1 / 0), Other = 1 || (1 / 0) };
int main(void) { return Safe != 9 || Short != 0 || Other != 1; }
