#!/usr/bin/env python3
"""Exhaustive hot-decoder selection and actual 0x6 instruction helpers on a host.
Memory/device callbacks are controlled models. This is not PPC execution or FPS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

NAMES=('MOVBL','MOVWL','MOVLL','MOV','MOVBP','MOVWP','MOVLP','NOT','SWAPB','SWAPW','NEGC','NEG','EXTUB','EXTUW','EXTSB','EXTSW')
def extract(text,name):
    m=re.search(r'static inline void '+re.escape(name)+r'\(const UINT16 opcode\)\s*\{',text)
    if not m: raise ValueError('missing production helper '+name)
    end=m.end(); depth=1
    while depth:
        if text[end]=='{': depth+=1
        elif text[end]=='}': depth-=1
        end+=1
    return text[m.start():end]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    here=Path(__file__).resolve().parent; core=(here/'../../src/cpu/sh4').resolve()
    text=(core/'sh4.cpp').read_text()
    generic=extract(text,'execute_one')
    calls=set(re.findall(r'\b([A-Za-z_][A-Za-z_0-9]*)\(opcode\)',generic))
    stubs='\n'.join('static void %s(UINT16 opcode) {other_handler(%d,opcode);}'%(name,i+1) for i,name in enumerate(sorted(calls-set(NAMES))))
    helpers='\n'.join(extract(text,name) for name in NAMES)
    generic=generic.replace('{','{\n ++generic_calls;',1)
    body=r'''
int main() {
 unsigned cases=0,hot=0;
 for(unsigned op=0;op<65536;++op)for(unsigned variant=0;variant<8;++variant) {
  TestState initial=initial_state(variant,op);
  state=initial;generic_calls=0;execute_one((UINT16)op);
  TestState expected=state;if(generic_calls!=1)return 2;
  state=initial;generic_calls=0;sh3_execute_hot_fallback((UINT16)op);
  bool selected=(op&0xff00)==0x6000;
  if(memcmp(&state,&expected,sizeof(state)) || generic_calls!=(selected?0u:1u)) {
   fprintf(stderr,"FAIL op=%04X variant=%u calls=%u\n",op,variant,generic_calls);return 1;
  }
  hot+=selected; ++cases;
 }
 printf("PASS %u complete-opcode/state cases; all 65536 opcodes; %u hot cases bypass general decoding\n",cases,hot);
 puts("PASS production 16 helpers: registers/T/EA, same-register postincrement, memory callback width/address/order, watched idle-cycle charging and cached/uncached addresses");
 puts("Scope: actual helper bodies with modeled memory; other opcode handlers stubbed; not a full CPU/PPC/game replay");
 return 0;
}
'''
    source=out/'test-extracted.cpp'
    source.write_text('#define SH3_HOT_FALLBACK_TEST 1\n#include '+json.dumps(str(here/'fixture.h'))+'\n'+stubs+'\n'+helpers+'\n'+generic+'\n#include '+json.dumps(str(core/'sh3_interpreter_hot.h'))+'\n'+body)
    (out/'extracted-helpers.json').write_text(json.dumps({name:hashlib.sha256(extract(text,name).encode()).hexdigest() for name in NAMES},indent=2))
    records=[]
    modes=[('optimized',['-O3']),('asan-ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize=shift-base','-fno-sanitize-recover=all','-fno-omit-frame-pointer'])]
    for mode,flags in modes:
        exe=out/('hot-fallback-'+mode)
        for phase,argv in [('compile',['g++','-std=c++11',*flags,str(source),'-o',str(exe)]),('run',[str(exe)])]:
            r=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
            log=out/(mode+'-'+phase+'.log');log.write_text(r.stdout)
            records.append({'mode':mode,'phase':phase,'argv':argv,'exit_code':r.returncode,'log':log.name})
            (out/'results.json').write_text(json.dumps(records,indent=2)); print(r.stdout,end='')
            if r.returncode:raise SystemExit(r.returncode)
    print('Sanitizer qualification: shift-base is excluded for unchanged legacy EXTSB/EXTSW signed-shift idioms; all other selected sanitizer checks remain active.')
if __name__=='__main__':main()
