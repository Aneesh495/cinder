static int n;int tick(void){++n;return n;}int main(void){if(tick())n+=2;return n;}
