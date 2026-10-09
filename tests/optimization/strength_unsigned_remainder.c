static volatile unsigned input=0xf0000035U; int main(void){unsigned x=input;return x%32U==21U?0:1;}
