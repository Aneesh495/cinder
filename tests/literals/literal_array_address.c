int main(void) { char (*p)[4] = &"abc"; return (*p)[2] - 90; }
