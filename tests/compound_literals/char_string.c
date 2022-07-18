int main(void) { char *p=(char[8]){"abc"}; p[1]='z'; return p[0]!='a' || p[1]!='z' || p[3]!=0 || p[7]!=0; }
