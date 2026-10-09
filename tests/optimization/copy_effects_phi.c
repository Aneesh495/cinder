static volatile int flag=1,input=37;static int events;int main(void){int x=input,r;if(flag){++events;r=x;}else{events+=2;r=x;}return r+events;}
