#!/usr/bin/env python3
"""Run presentation policy and extracted notification-adapter tests on a host."""
import argparse
import json
from pathlib import Path
import subprocess


def extract_function(text, declaration):
    start = text.index(declaration)
    opening = text.index('{', start)
    depth = 1
    for index in range(opening + 1, len(text)):
        if text[index] == '{':
            depth += 1
        elif text[index] == '}':
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise ValueError('Unbalanced function: ' + declaration)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    tests = Path(__file__).resolve().parent
    video = (tests / '../../SDL/src/video/xbox').resolve()
    text = (video / 'SDL_xboxvideo.c').read_text(encoding='utf-8')
    adapter = out / 'notification-extracted.c'
    adapter.write_text(
        '#include ' + json.dumps(str(video / 'SDL_xbox_ui_recovery.h')) + '\n' +
        '#include ' + json.dumps(str(tests / 'notification_fixture.h')) + '\n' +
        extract_function(text, 'static void XBOX_CoreUiRequest(int enabled)') + '\n' +
        extract_function(text, 'static unsigned XBOX_CoreRecoveryEpoch(void* unused)') + '\n',
        encoding='utf-8')
    records = []
    for case, source in [('policy', tests / 'test_async_present.c'), ('notification', adapter)]:
        for mode, flags in [('optimized', ['-O2']), ('asan-ubsan', ['-O1', '-g',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer'])]:
            executable = out / (case + '-' + mode)
            compiler = ['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror', *flags,
                        str(source), '-o', str(executable)]
            for phase, argv in [('compile', compiler), ('run', [str(executable)])]:
                result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=120, check=False)
                log = out / (case + '-' + mode + '-' + phase + '.log')
                log.write_text(result.stdout, encoding='utf-8')
                records.append(dict(case=case, mode=mode, phase=phase, argv=argv,
                                    exit_code=result.returncode, log=log.name))
                (out / 'results.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
                print(result.stdout, end='')
                if result.returncode:
                    raise SystemExit(result.returncode)
    print('PASS 4 host executions; native build and console recovery remain separate checks')


if __name__ == '__main__':
    main()
