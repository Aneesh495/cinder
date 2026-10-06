extern char text[]; int read(void) { return text[1]; } char text[]="abc"; int main(void) { return read(); }
