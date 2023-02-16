#!/usr/bin/env python3
"""Write or independently check frozen, authored application input/output fixtures."""
import argparse
import bisect
import collections
from fractions import Fraction
import json
from pathlib import Path
import zlib


def fixtures():
    files = {}; applications = []
    def application(name, units, description):
        record = dict(name=name, units=['examples/apps/' + p for p in units], description=description, interpreter_exclusion='External FILE/stdio, argv or libc calls are outside the interpreter profile.', cases=[])
        applications.append(record); return record
    def case(app, name, data=b'', argv=(), stdout=b'', stderr=b'', exit=0, output=None, previous=None):
        prefix = app['name'] + '-' + name
        def file(suffix, content):
            path = prefix + suffix; files[path] = content.encode() if isinstance(content, str) else content; return path
        record = dict(name=name, stdin=file('.in', data), argv=[a.replace('@INPUT', '$FIXTURES/' + prefix + '.in') for a in argv], stdout=file('.out', stdout), stderr=file('.err', stderr), exit=exit, files={})
        if output is not None: record['files']['result.txt'] = file('.result', output)
        if previous is not None: record['previous_files'] = {'result.txt': file('.previous', previous)}
        app['cases'].append(record)
    words = application('word-frequency', ['hash_words.c','wordfreq/table.c'], 'Growing open-addressed FNV hash table, case folding, tokenization and callback sorting.')
    def word_case(name, data, argv=()):
        import re
        tokens = re.findall(r'[a-z0-9_]+', data.decode().lower()); counts = collections.Counter(tokens)
        out = f'tokens={len(tokens)} unique={len(counts)}\n' + ''.join(f'{key} {counts[key]}\n' for key in sorted(counts))
        case(words, name, data, argv, out)
    word_case('prose', b'Cinder cinder, compiler! ARRAY array _value 42\ncompiler CINDER\n')
    word_case('rehash', (' '.join(f'word{i:03d}' for i in range(180)) + '\n' + ' '.join(f'WORD{i:03d}' for i in range(0,180,3))).encode(), ['@INPUT'])
    word_case('empty', b'')
    case(words, 'long-token', b'x'*256, stderr='word-frequency: invalid or oversized input\n', exit=2)
    graph = application('graph', ['graph.c','graph/algorithms.c'], 'Directed adjacency lists, BFS hops and binary-heap Dijkstra distances.')
    def graph_case(name,n,edges,source,argv=()):
        inf = 10**9; distances = [[inf]*n for _ in range(n)]; successors = [[] for _ in range(n)]
        for i in range(n): distances[i][i] = 0
        for a,b,w in edges: distances[a][b] = min(distances[a][b], w); successors[a].append(b)
        for k in range(n):
            for i in range(n):
                for j in range(n): distances[i][j] = min(distances[i][j], distances[i][k]+distances[k][j])
        hops = {source:0}; queue = collections.deque([source])
        while queue:
            a = queue.popleft()
            for b in successors[a]:
                if b not in hops: hops[b] = hops[a]+1; queue.append(b)
        out = f'nodes={n} edges={len(edges)} source={source}\n'
        out += ''.join(f'{i} unreachable\n' if distances[source][i] == inf else f'{i} distance={distances[source][i]} hops={hops[i]}\n' for i in range(n))
        data = f'{n} {len(edges)} {source}\n'+''.join(f'{a} {b} {w}\n' for a,b,w in edges)
        case(graph,name,data,argv,out)
    graph_case('cycles',7,[(0,1,9),(0,2,1),(2,1,2),(1,3,3),(2,3,20),(3,4,0),(4,2,2),(0,1,8),(5,6,1)],0,['@INPUT'])
    graph_case('singleton',1,[],0)
    graph_case('pressure',40,[(a,b,(a*17+b*7)%101) for a in range(40) for b in range(40) if (a*13+b*19)%11==0],3)
    case(graph,'bad-edge','3 1 0\n0 3 1\n',stderr='graph: invalid input\n',exit=2)
    parser = application('json', ['json.c'], 'Bounded recursive ASCII JSON parser with Unicode escape and surrogate validation.')
    class Pairs(list): pass
    def json_case(name,text,argv=()):
        obj = json.loads(text,object_pairs_hook=Pairs); counts = dict(objects=0,arrays=0,strings=0,numbers=0,booleans=0,nulls=0,codepoints=0,depth=0)
        def walk(obj,depth):
            counts['depth'] = max(counts['depth'],depth)
            if isinstance(obj,Pairs):
                counts['objects'] += 1
                for key,value in obj:
                    counts['strings'] += 1; counts['codepoints'] += len(key); walk(value,depth+1)
            elif isinstance(obj,list):
                counts['arrays'] += 1
                for value in obj: walk(value,depth+1)
            elif isinstance(obj,str): counts['strings'] += 1; counts['codepoints'] += len(obj)
            elif isinstance(obj,bool): counts['booleans'] += 1
            elif obj is None: counts['nulls'] += 1
            else: counts['numbers'] += 1
        walk(obj,1);case(parser,name,text,argv,' '.join(f'{k}={v}' for k,v in counts.items())+'\n')
    json_case('nested','{"title":"Cinder","values":[0,-12,3.25,1e3],"flags":[true,false,null],"nested":{"empty":[],"unicode":"\\u0041\\ud83d\\ude00"}}\n',['@INPUT'])
    json_case('duplicate-keys',' { "key": 1, "key": ["a\\nb", {}, []] } ')
    json_case('empty','{}')
    json_case('depth-boundary','['*127+'0'+']'*127)
    case(parser,'raw-nonascii',b'"\xc3\xa9"',stderr='json: invalid or unsupported input\n',exit=2)
    for name,text in [('leading-zero','[01]'),('surrogate','"\\ud800x"'),('trailing-comma','[1,]'),('depth','['*129+'0'+']'*129)]: case(parser,name,text,stderr='json: invalid or unsupported input\n',exit=2)
    vm = application('bytecode',['bytecode.c'],'Text bytecode loader and bounded stack VM with locals, branches, arithmetic and output.')
    def vm_case(name,text):
        code = [line.split('#',1)[0].split() for line in text.splitlines()];code = [p for p in code if p]
        stack=[];locals=[0]*16;pc=steps=0;out=''
        while True:
            steps+=1;op,*arg=code[pc];pc+=1;value=int(arg[0]) if arg else None
            if op=='push':stack.append(value)
            elif op=='load':stack.append(locals[value])
            elif op=='store':locals[value]=stack.pop()
            elif op in ('add','sub','mul','lt'):
                b=stack.pop();a=stack.pop();stack.append(a+b if op=='add' else a-b if op=='sub' else a*b if op=='mul' else int(a<b))
            elif op=='jz':
                if not stack.pop():pc=value
            elif op=='jmp':pc=value
            elif op=='dup':stack.append(stack[-1])
            elif op=='print':out+=str(stack.pop())+'\n'
            elif op=='halt':out+=f'halt steps={steps} stack={len(stack)}\n';break
            else:raise AssertionError(op)
            assert steps<100000
        case(vm,name,text,['@INPUT'],out)
    vm_case('sum-loop','push 0\nstore 0\npush 0\nstore 1\nload 1\npush 10\nlt\njz 17\nload 0\nload 1\nadd\nstore 0\nload 1\npush 1\nadd\nstore 1\njmp 4\nload 0\nprint\nhalt\n')
    vm_case('factorial','push 1\nstore 0\npush 1\nstore 1\nload 1\npush 7\nlt\njz 17\nload 0\nload 1\nmul\nstore 0\nload 1\npush 1\nadd\nstore 1\njmp 4\nload 0\ndup\nprint\nhalt\n')
    vm_case('inline-comment','push 7 # operand\nprint# output\nhalt# stop\n')
    case(vm,'embedded-nul',b'halt\0ignored\n',['@INPUT'],stderr='bytecode: invalid program or execution limit\n',exit=2)
    for name,text in [('underflow','add\nhalt\n'),('loop','jmp 0\n'),('bad-opcode','launch 4\n')]:case(vm,name,text,['@INPUT'],stderr='bytecode: invalid program or execution limit\n',exit=2)
    sorting=application('sort-search',['sort_search.c'],'Stable merge sort checked against callback qsort; lower/upper bound queries and dynamic input.')
    def sort_case(name,values,queries):
        ordered=sorted(enumerate(values),key=lambda p:p[1]);sequence=[v for _,v in ordered]
        checksum=sum((i+1)*(v+1000000)+original for i,(original,v) in enumerate(ordered))
        out=f'count={len(values)} min={min(values) if values else 0} max={max(values) if values else 0} checksum={checksum}\n'
        out+=''.join(f'query={v} lower={bisect.bisect_left(sequence,v)} upper={bisect.bisect_right(sequence,v)}\n' for v in queries)
        case(sorting,name,' '.join(map(str,values))+'\n',list(map(str,queries)),out)
    sort_case('duplicates',[9,-4,9,0,-4,1000000,-1000000,8,8,8],[-1000000,-4,8,10,1000000])
    sort_case('empty',[],[0])
    sort_case('reverse',list(range(999,-1,-1))+[32]*50,[0,32,999,1000])
    case(sorting,'invalid','1 2 rubbish 4',stderr='sort-search: invalid input or allocation limit\n',exit=2)
    crc=application('checksums',['crc.c'],'Streaming IEEE CRC32, checked bitwise implementation, Adler32 and FNV-1a over arbitrary bytes.')
    def crc_case(name,data,argv=()):
        fnv=14695981039346656037
        for byte in data:fnv=((fnv^byte)*1099511628211)&((1<<64)-1)
        out=f'bytes={len(data)} crc32={zlib.crc32(data):08x} adler32={zlib.adler32(data):08x} fnv1a={fnv:016x}\n'
        case(crc,name,data,argv,out)
    crc_case('standard',b'123456789')
    crc_case('empty',b'')
    crc_case('binary',bytes(range(256))*37,['@INPUT'])
    matrix=application('matrix',['matrix.c'],'Blocked dense binary64 multiplication with independent row/column checks and exact binary-fraction inputs.')
    def matrix_case(n,rounds,seed):
        state=seed;a=[];b=[]
        for _ in range(n*n):
            state=(state*1664525+1013904223)&((1<<32)-1);a.append(state%65-32)
            state=(state*1664525+1013904223)&((1<<32)-1);b.append(state%65-32)
        product=[[sum(a[i*n+k]*b[k*n+j] for k in range(n)) for j in range(n)] for i in range(n)]
        checksum=Fraction(sum(product[i][j]*(i*n+j+1) for i in range(n) for j in range(n))*rounds,64)
        trace=Fraction(sum(product[i][i] for i in range(n))*rounds,64)
        case(matrix,f'{n}x{n}',argv=list(map(str,(n,rounds,seed))),stdout=f'dimension={n} rounds={rounds} checksum={float(checksum):.6f} trace={float(trace):.6f}\n')
    matrix_case(1,1,0);matrix_case(7,2,42);matrix_case(17,3,123456)
    case(matrix,'invalid',argv=['0','1','0'],stderr='matrix: DIMENSION[1..64] ROUNDS[1..10] SEED[uint32]\n',exit=2)
    records=application('records-cli',['cli_multi.c','records/records.c'],'Multi-file CSV CLI: dynamic records, callback sorting, grouped aggregates and selected output files.')
    data='beta,10\nalpha,4\nbeta,-5\ngamma,0\nalpha,8\nbeta,10\n'
    case(records,'group',data,['group','@INPUT','$WORK/result.txt'],output='records=6 groups=3\nalpha count=2 sum=12 min=4 max=8\nbeta count=3 sum=15 min=-5 max=10\ngamma count=1 sum=0 min=0 max=0\n')
    case(records,'select',data,['select','@INPUT','$WORK/result.txt','0','8'],output='gamma,0\nalpha,4\nalpha,8\nselected=3\n')
    case(records,'empty','',['group','@INPUT','$WORK/result.txt'],output='records=0 groups=0\n')
    case(records,'bad-row','alpha,1\nbad,not-a-number\n',['group','@INPUT','$WORK/result.txt'],stderr='records: invalid input or allocation limit\n',exit=2,previous='complete prior output\n',output='complete prior output\n')
    case(records,'embedded-nul',b'alpha,1\0ignore,2\n',['group','@INPUT','$WORK/result.txt'],stderr='records: invalid input or allocation limit\n',exit=2,previous='complete prior output\n',output='complete prior output\n')
    manifest=dict(schema=1,applications=applications)
    files['cases.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    return files


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');args=parser.parse_args()
    root=Path('examples/apps/fixtures');expected=fixtures()
    if args.check:
        assert {p.name for p in root.iterdir()}==set(expected)
        assert all((root/name).read_bytes()==data for name,data in expected.items())
        print(f'Application fixtures: {len(expected)} frozen input/output records match independent oracles')
    else:
        root.mkdir(parents=True,exist_ok=True)
        for name,data in expected.items():(root/name).write_bytes(data)


if __name__=='__main__':main()
