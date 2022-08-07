int calls; int f(void){++calls;return 7;} int main(void){_Alignas(sizeof f()) int x=3;return calls || x!=3;}
