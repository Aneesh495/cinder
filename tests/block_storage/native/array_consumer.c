int consume(void) { extern int values[]; static int *p=&values[1]; ++*p; return values[0]+values[1]+values[2]; } int main(void) { return consume()!=13 || consume()!=14; }
