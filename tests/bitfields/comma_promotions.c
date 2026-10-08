struct S { unsigned a:3; };int main(void){struct S s={5};_Static_assert(_Generic((0,((struct S*)0)->a)<<0,int:1,default:0),"comma promotion");return _Generic((0,s.a)<<0,int:0,default:1) || s.a!=5;}
