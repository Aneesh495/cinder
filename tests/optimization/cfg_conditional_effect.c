static int n;int bump(void){++n;return 13;}int main(void){int x=1?7:bump();return x+n;}
