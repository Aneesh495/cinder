static int calls; static unsigned next(void){++calls;return 35U;} int main(void){unsigned x=next()%8U;return x==3U&&calls==1?0:1;}
