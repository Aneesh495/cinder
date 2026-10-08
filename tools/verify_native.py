#!/usr/bin/env python3
"""Inspect retained native reports; never rebuild or execute their artifacts."""
import argparse
import json
import pathlib
import sys

from evidence_integrity import digest
from native_evidence import verify_bootstrap, verify_generated


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('mode',choices=('bootstrap','generated'))
    parser.add_argument('--artifacts',type=pathlib.Path,required=True)
    parser.add_argument('--source',type=pathlib.Path,default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument('--compiler',action='append',required=True,help='bootstrap seed path or generated name=path')
    parser.add_argument('--minimum',type=int,default=20000)
    args=parser.parse_args()
    try:
        if args.mode=='bootstrap':
            if len(args.compiler)!=1:
                raise ValueError('bootstrap requires one seed compiler')
            result=verify_bootstrap(args.artifacts,args.source,args.compiler[0])
        else:
            compilers={}
            for entry in args.compiler:
                name,path=entry.split('=',1)
                if name in compilers or name in ('gcc','clang'):
                    raise ValueError('duplicate or reserved original compiler name')
                compilers[name]=digest(pathlib.Path(path))
            if args.minimum<1:
                raise ValueError('minimum must be positive')
            result=verify_generated(args.artifacts,args.source,args.minimum,compilers)
    except (OSError,ValueError,KeyError,TypeError,AttributeError) as error:
        print('native evidence verification failed: '+str(error),file=sys.stderr)
        return 1
    print(json.dumps(result,sort_keys=True))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
