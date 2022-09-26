static int calls;int choose(void){++calls;return 258;}int main(void){unsigned char c=(unsigned char)choose();if(c==2)return calls;return 9;}
