static volatile unsigned input=0xf0000007U; int main(void){unsigned x=input;return x/8U==0x1e000000U?0:1;}
