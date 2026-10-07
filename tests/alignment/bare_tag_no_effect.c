_Alignas(16) struct P{int x;}; int main(void){return _Alignof(struct P)!=4 || sizeof(struct P)!=4;}
