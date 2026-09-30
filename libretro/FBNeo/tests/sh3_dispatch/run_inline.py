#!/usr/bin/env python3
"""Run host control/data regressions for inlined SH3 dispatch, not console FPS.

Example: python3 run_inline.py --output /tmp/salvia-inline-tests
Requires a host g++ with AddressSanitizer and UndefinedBehaviorSanitizer.
The output directory is explicit so executables/logs need not enter the checkout.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler', default='g++')
    args = parser.parse_args()
    compiler = shutil.which(args.compiler)
    if compiler is None:
        parser.error('C++ compiler is not available: ' + args.compiler)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parent.parent
    cases = ('sh3_dispatch', 'sh3_lookup', 'sh3_source_check', 'sh3_block_layout')
    modes = {
        'optimized': ['-O3'],
        'asan-ubsan': ['-O1', '-g', '-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-fno-sanitize-recover=all'],
    }
    records = []
    for case in cases:
        for mode, flags in modes.items():
            executable = output / (case + '-' + mode)
            command = [compiler, '-std=c++11', *flags]
            if case == 'sh3_dispatch':
                command.append('-DSH3_CACHE_READY_EXPECT_FAST')
            command += [str(tests / case / 'test.cpp'), '-o', str(executable)]
            for phase, argv in (('compile', command), ('run', [str(executable)])):
                result = subprocess.run(argv, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True,
                                        timeout=120, check=False)
                log = output / (case + '-' + mode + '-' + phase + '.log')
                log.write_text(result.stdout, encoding='utf-8')
                records.append({'case': case, 'mode': mode, 'phase': phase,
                                'argv': argv, 'exit_code': result.returncode,
                                'log': log.name})
                (output / 'results.json').write_text(
                    json.dumps(records, indent=2), encoding='utf-8')
                if result.stdout:
                    print(result.stdout, end='')
                if result.returncode:
                    raise SystemExit('FAILED: %s %s %s (exit %s)' %
                                     (case, mode, phase, result.returncode))
    print('PASS 4 host suites in optimized and ASan/UBSan modes')
    print('Scope: synthetic callbacks and data validation; not PPC execution or FPS')


if __name__ == '__main__':
    main()
