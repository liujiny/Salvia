#!/usr/bin/env python3
"""Compare the real outer-loop source with a preserved pre-instrumentation sh4.cpp."""
import argparse
import json
from pathlib import Path
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline-sh4',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    tests=Path(__file__).resolve().parent
    candidate=(tests/'../../src/cpu/sh4/sh4.cpp').resolve().read_text()
    old=a.baseline_sh4.read_text()
    begin='static int Sh3Run_timerhack(int cycles)'
    end='static int Sh3Run_normal(int cycles)'
    reference=old[old.index(begin):old.index(end)].replace('Sh3Run_timerhack(', 'reference_timerhack(',1)
    start='template<bool Count> static int Sh3Run_timerhack_impl(int cycles)'
    body=candidate[candidate.index(start):candidate.index(begin)]
    source=out/'outer-extracted.cpp'
    source.write_text('#include '+json.dumps(str(tests/'outer_fixture.h'))+'\n'+reference+'\n'+body)
    records=[]
    for mode,flags in [('optimized',['-O3']),('asan-ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]:
        exe=out/('outer-'+mode)
        for phase,argv in [('compile',['g++','-std=c++11',*flags,str(source),'-o',str(exe)]),('run',[str(exe)])]:
            r=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
            log=out/(mode+'-'+phase+'.log');log.write_text(r.stdout)
            records.append({'mode':mode,'phase':phase,'argv':argv,'exit_code':r.returncode,'log':log.name})
            (out/'results.json').write_text(json.dumps(records,indent=2))
            print(r.stdout,end='')
            if r.returncode:raise SystemExit(r.returncode)

if __name__=='__main__':main()
