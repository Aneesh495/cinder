#define X foo
#define P(x,y) x##y
#define PP(x,y) P(x,y)
PP(X,bar)
