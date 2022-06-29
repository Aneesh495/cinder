int main(void) { struct P { char *p; }; struct P q={"abc"}; q.p[0]=1; return 0; }
