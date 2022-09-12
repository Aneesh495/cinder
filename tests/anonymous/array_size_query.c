struct S{union{int a[3];double d;};};_Static_assert(sizeof(((struct S *)0)->a)==3*sizeof(int),"promoted unevaluated array");int main(void){return 0;}
