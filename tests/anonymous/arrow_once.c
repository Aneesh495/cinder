struct S{union{int n;double d;};};static struct S s={.n=5};static int calls;static struct S *get(void){++calls;return &s;}int main(void){get()->n+=7;return calls!=1||s.n!=12;}
