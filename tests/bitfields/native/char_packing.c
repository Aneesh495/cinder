struct S { char tag; unsigned a:3; signed b:5; char end; };
extern struct S reference(struct S (*callback)(struct S),struct S s,int a,int b,int c,int d,int e,int f,int g);
static struct S transform(struct S s){s.a+=1;s.b-=2;return s;}
int main(void){struct S s={'Q',2,-3,'Z'};s=reference(transform,s,1,2,3,4,5,6,7);return !(s.a==4&&s.b==-7&&s.tag=='Q'&&s.end=='Z');}
