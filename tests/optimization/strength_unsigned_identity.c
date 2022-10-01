static volatile unsigned input=0x80000003U; int main(void){unsigned x=input;return x*1U==0x80000003U?0:1;}
