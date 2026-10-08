"""Bounded C17 programs with independently calculated exit observations.

All signed intermediate values fit int, floating values are exact binary
fractions, pointer arithmetic stays in initialized arrays, recursive depth is
bounded, and no result depends on argument evaluation order or padding.
"""
import random


def program(seed, index):
    rng = random.Random((seed << 32) ^ index)
    values = [rng.randrange(1, 128) for _ in range(rng.randrange(4, 12))]
    a, b, c, d = values[:4]
    family = index % 9
    initializer = ','.join(str(v) for v in values)
    n = len(values)
    prefix = f'static volatile int input[{n}]={{{initializer}}};\n'
    if family == 0:
        # Unsigned arithmetic has an explicitly modular independent oracle.
        shift = rng.randrange(1, 6)
        expected = ((a + b) ^ (c << shift) ^ (d * 3)) & 255
        body = f'unsigned a=(unsigned)input[0],b=(unsigned)input[1],c=(unsigned)input[2],d=(unsigned)input[3];return (int)(((a+b)^(c<<{shift})^(d*3U))&255U);'
        helpers = ''
        name = 'unsigned-arithmetic'
    elif family == 1:
        expected = sum((v * (i + 1)) for i, v in enumerate(values)) & 255
        body = f'int copy[{n}];for(int i=0;i<{n};++i)copy[i]=input[i];int *p=copy;int total=0;for(int i=0;i<{n};++i)total+=p[i]*(i+1);return total&255;'
        helpers = ''
        name = 'array-pointer-loop'
    elif family == 2:
        expected = (a + c + (b + d) * 2) & 255
        helpers = 'struct Pair{int n;double weight;};static struct Pair combine(struct Pair a,struct Pair b){struct Pair r={a.n+b.n,a.weight+b.weight};return r;}\n'
        body = 'struct Pair a={input[0],input[1]*0.5},b={input[2],input[3]*0.5};struct Pair r=combine(a,b);return (r.n+(int)(r.weight*4.0))&255;'
        name = 'mixed-aggregate-return'
    elif family == 3:
        expected = (3 * a + b + d + c) & 255
        helpers = 'static int transform(int a,int b){return 3*a+b;}static int apply(int (*f)(int,int),int a,int b){return f(a,b);}\n'
        body = 'int (*callback)(int,int)=transform;int value=apply(callback,input[0],input[1]);return (value+input[3]+input[2])&255;'
        name = 'callback'
    elif family == 4:
        depth = rng.randrange(1, 9)
        expected = (a + depth * b) & 255
        helpers = 'static int recurse(int n,int a,int step){if(n==0)return a;return recurse(n-1,a+step,step);}\n'
        body = f'return recurse({depth},input[0],input[1])&255;'
        name = 'bounded-recursion'
    elif family == 5:
        expected = (a + b * 2 + c * 3 + d * 4) & 255
        helpers = 'struct Cell{short value;unsigned char factor;};\n'
        body = 'struct Cell cells[4];for(int i=0;i<4;++i){cells[i].value=(short)input[i];cells[i].factor=(unsigned char)(i+1);}int total=0;for(int i=0;i<4;++i)total+=cells[i].value*cells[i].factor;return total&255;'
        name = 'aggregate-array-conversions'
    elif family == 6:
        expected = sum(v if v % 4 == 0 else v * 2 if v % 4 == 1 else v + 3 if v % 4 == 2 else v - 1 for v in values) & 255
        helpers = ''
        body = f'int total=0;for(int i=0;i<{n};++i){{int value=input[i];switch(value&3){{case 0:total+=value;break;case 1:total+=2*value;break;case 2:total+=value+3;break;default:total+=value-1;break;}}}}return total&255;'
        name = 'switch-control'
    elif family == 7:
        expected = (a * 3 + b * 5 + c + d) & 255
        declaration = 'int helper(int a,int b);\n'
        provider = 'int helper(int a,int b){return a*3+b*5;}\n'
        source = prefix + declaration + 'int main(void){return (helper(input[0],input[1])+input[2]+input[3])&255;}\n'
        return dict(family='multiple-units', expected=expected, units={'main.c': source, 'provider.c': provider}, interpreter=prefix + provider + 'int main(void){return (helper(input[0],input[1])+input[2]+input[3])&255;}\n')
    else:
        left = a < 64 and b > 32
        right = b > 96 or a > 16
        effects = int(a < 64) + int(b <= 96)
        expected = ((a + b if left else a + 2 * b) + effects * 7 + int(right) * 13) & 255
        helpers = ''
        body = 'int x=input[0],y=input[1],effects=0;int left=x<64&&(++effects,y>32);int right=y>96||(++effects,x>16);int result=left?x+y:x+2*y;return (result+effects*7+right*13)&255;'
        name = 'short-circuit-effects'
    source = prefix + helpers + 'int main(void){' + body + '}\n'
    return dict(family=name, expected=expected, units={'main.c': source}, interpreter=source)
