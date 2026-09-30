#!/usr/bin/env python3
import argparse, pathlib, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--output',type=pathlib.Path);args=p.parse_args()
here=pathlib.Path(__file__).resolve().parent;repo=here.parents[3]
output=args.output or pathlib.Path(tempfile.mkdtemp(prefix='frontend-state-'));output.mkdir(parents=True,exist_ok=True)
cmd=['g++','-std=c++98','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-I'+str(here/'stubs'),'-I'+str(repo/'src'),str(here/'test.cpp'),'-pthread','-lz','-o',str(output/'test')]
if args.sanitize:cmd[2:3]=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);subprocess.run([str(output/'test'),str(output)],check=True)
