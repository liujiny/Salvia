#!/usr/bin/env python3
"""Build and check the real Z80 core against its normal instruction loop."""
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'tchar.h').write_text('''#pragma once
#include <stdint.h>
#define _T(x) x
#define __fastcall
typedef char TCHAR;
''')
    args = ['g++', '-std=c++98', '-O2', '-UNDEBUG', '-DLSB_FIRST',
            '-DFBNEO_Z80_STATUS_POLL_TEST', '-fsanitize=address,undefined',
            '-Wno-write-strings', '-I' + str(path)]
    args += ['-I' + str(root / p) for p in
             ['src/burn', 'src/burn/snd', 'src/burn/devices', 'src/cpu', 'src/cpu/z80']]
    subprocess.run(args + [str(here / 'test.cpp'), str(root / 'src/cpu/z80/z80.cpp'),
                           '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
