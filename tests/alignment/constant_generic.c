int main(void){_Alignas(_Generic(1,int:16,default:8)) char x=3;return (unsigned long)&x%16 || x!=3;}
