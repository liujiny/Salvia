#!/usr/bin/env python3
from pathlib import Path
import shutil, sys, hashlib, datetime, json

PKG = Path(__file__).resolve().parent

ALLOWED = {
    'libretro/mame-2003-plus/src/cpu/mips/mips3.c': {
        '21b1d6d33d11704e876178997e4ca884465a89a1263248d5e547044b654259ee',
        '196df0b405eece2ce0b9186d92830c9ce4995262d8c600c4f3ee9a13da0b5f35',
    },
    'libretro/mame-2003-plus/src/cpu/mips/mips3.h': {
        'dfe564ce3b2ab7a53f920f93747138be9a9c219d808ed5b12fa30e15f8651225',
        '2e5d403da859f39e15e884a319b9c0612ebecc99beba8ff617994a3e308417b5',
    },
    'libretro/mame-2003-plus/src/drivers/kinst.c': {
        '582bb1d85b7ee0848a51b50f04fc870985c6ff11131bff2f34c8d099356db028',
        'd3cf8e6e25858423ddca7b982c61ee6cad5a969ae7ccfe4bb1f5205cfe7ff03e',
    },
    'libretro/mame-2003-plus/src/mame2003/mame2003.c': {
        'ac5ebe92869dcf704f7ad3707bd786b852309b228636a14dd595f24cf9fde30f',
        '5dbccafec1e7d322adb1d66fb3b340786705dd35498e515d0e13285844c9037f',
        'def26bd26be13d4ff6ec3d3ad41f8190550e6f3227aaefcf7b40ea5cf313b63e',
    },
    'libretro/mame-2003-plus/src/mame2003/video.c': {
        'ce3fed413b2ce7db37ef3a203f587a5f9d3d82d9f711faf1318644f7deb20af4',
        '2f22b5c79eaa024567bfeb7bc1dd07f9e78506fc83deb8d0877cbb4b4f5c154b',
        '949085d2dc2b57dec1bf35b7769a272c79e1b53943b9fa8e14e603734a7048e6',
        'c66564dce6dce3f4339f6b2f401e776d1c048048bb5052e20604ace2e49d8873',
        'd424dda155d7ca7938d2cd24b6fb4bec14b0cf2b95a67044327fc4e3aae05c2f',
        '2ca0a5f351ebbb798e9d9ea5718eca5352710677ceaa197ceeb9bdebea30e133',
    },
    'src/salvia.cpp': {
        '070d36d571b3e7ef5e0a250b3e07d95b674a4015fb7bc1e1f6973cd80c5ceefe',
        'a2f0f67b1458d57529cbb5da08ba5106511a2351fe74fd4faad6c3aef2679a99',
        '9595406465c4e971a3d1740a59b6fa8ba6aca9b1ffe8dc92458656d8725e9a0a',
    },
}

TARGET_HASH = {
    'libretro/mame-2003-plus/src/cpu/mips/mips3.c': '196df0b405eece2ce0b9186d92830c9ce4995262d8c600c4f3ee9a13da0b5f35',
    'libretro/mame-2003-plus/src/cpu/mips/mips3.h': '2e5d403da859f39e15e884a319b9c0612ebecc99beba8ff617994a3e308417b5',
    'libretro/mame-2003-plus/src/drivers/kinst.c': 'd3cf8e6e25858423ddca7b982c61ee6cad5a969ae7ccfe4bb1f5205cfe7ff03e',
    'libretro/mame-2003-plus/src/mame2003/mame2003.c': 'def26bd26be13d4ff6ec3d3ad41f8190550e6f3227aaefcf7b40ea5cf313b63e',
    'libretro/mame-2003-plus/src/mame2003/video.c': '2ca0a5f351ebbb798e9d9ea5718eca5352710677ceaa197ceeb9bdebea30e133',
    'src/salvia.cpp': '9595406465c4e971a3d1740a59b6fa8ba6aca9b1ffe8dc92458656d8725e9a0a',
}

COPY_FROM = {
    'libretro/mame-2003-plus/src/cpu/mips/mips3.c': 'core/src/cpu/mips/mips3.c',
    'libretro/mame-2003-plus/src/cpu/mips/mips3.h': 'core/src/cpu/mips/mips3.h',
    'libretro/mame-2003-plus/src/drivers/kinst.c': 'core/src/drivers/kinst.c',
    'libretro/mame-2003-plus/src/mame2003/mame2003.c': 'core/src/mame2003/mame2003.c',
    'libretro/mame-2003-plus/src/mame2003/video.c': 'core/src/mame2003/video.c',
    'src/salvia.cpp': 'frontend/src/salvia.cpp',
}

SDL_REL = 'libs/libSDLx360/SDL/src/video/xbox/SDL_xboxvideo.c'
INC_REL = 'libs/libSDLx360/SDL/src/video/xbox/SDL_xbox_mamepalette.inc'


def normalized_bytes(path: Path) -> bytes:
    b = path.read_bytes()
    if b.startswith(b'\xef\xbb\xbf'):
        b = b[3:]
    return b.replace(b'\r\n', b'\n').replace(b'\r', b'\n')


def nhash(path: Path) -> str:
    return hashlib.sha256(normalized_bytes(path)).hexdigest()


def text_style(path: Path):
    b = path.read_bytes()
    bom = b.startswith(b'\xef\xbb\xbf')
    if bom:
        b = b[3:]
    nl = '\r\n' if b'\r\n' in b else '\n'
    return bom, nl


def write_package_text_preserve_style(src: Path, dst: Path):
    bom, nl = text_style(dst) if dst.exists() else (False, '\r\n')
    b = src.read_bytes()
    if b.startswith(b'\xef\xbb\xbf'):
        b = b[3:]
    s = b.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n')
    out = s.replace('\n', nl).encode('utf-8')
    if bom:
        out = b'\xef\xbb\xbf' + out
    dst.write_bytes(out)


def patch_sdl_text(raw: bytes):
    bom = raw.startswith(b'\xef\xbb\xbf')
    if bom:
        raw = raw[3:]
    text = raw.decode('utf-8')
    nl = '\r\n' if '\r\n' in text else '\n'
    text = text.replace('\r\n', '\n').replace('\r', '\n')

    required = [
        'D3D_Device', 'g_current_effect', 'XBOX_SelectEffect',
        'XBOX_DrawMainQuad', 'XBOX_VideoQuit',
        'g_xboxFlipCS', 'g_xboxFlipCSInit'
    ]
    missing = [x for x in required if x not in text]
    if missing:
        raise RuntimeError('SDL driver lacks required symbols: ' + ', '.join(missing))

    include_hook = '#include "SDL_xbox_mamepalette.inc"'
    bind_hook = 'XBOX_MameIndexedBindForMainQuad();'
    shutdown_hook = 'XBOX_MameIndexedShutdown();'
    for hook in (include_hook, bind_hook, shutdown_hook):
        if text.count(hook) > 1:
            raise RuntimeError('SDL driver contains duplicate Round4G hook: ' + hook)

    if include_hook not in text:
        anchor = (
            'static void XBOX_SetSampler0Filter(D3DTEXTUREFILTERTYPE filter)\n'
            '{\n'
            '    g_current_sampler_filter = filter;\n'
            '    IDirect3DDevice9_SetSamplerState(D3D_Device, 0, D3DSAMP_MINFILTER, filter);\n'
            '    IDirect3DDevice9_SetSamplerState(D3D_Device, 0, D3DSAMP_MAGFILTER, filter);\n'
            '}\n'
        )
        if anchor not in text:
            raise RuntimeError('SDL sampler anchor not found; this SDL_xboxvideo.c is not the tested compatible layout.')
        text = text.replace(
            anchor,
            anchor + '\n/* Round4G liujiny-base: MAME indexed palette -> Xenos. */\n' + include_hook + '\n',
            1
        )

    if bind_hook not in text:
        anchor = (
            'static void XBOX_DrawMainQuad(void)\n'
            '{\n'
            '\tIDirect3DDevice9_SetScissorRect(D3D_Device, &g_visible_rect);\n'
        )
        if anchor not in text:
            raise RuntimeError('SDL main-quad anchor not found.')
        repl = (
            'static void XBOX_DrawMainQuad(void)\n'
            '{\n'
            '\tXBOX_MameIndexedBindForMainQuad();\n'
            '\tIDirect3DDevice9_SetScissorRect(D3D_Device, &g_visible_rect);\n'
        )
        text = text.replace(anchor, repl, 1)

    if shutdown_hook not in text:
        anchor = (
            '\t HLSLBackground_shutdown();\n'
            '\t XBOX_DestroyOverlay();\n'
            '\t if (this->hidden->SDL_primary)\n'
        )
        if anchor not in text:
            raise RuntimeError('SDL shutdown anchor not found.')
        repl = (
            '\t HLSLBackground_shutdown();\n'
            '\t XBOX_DestroyOverlay();\n'
            '\t XBOX_MameIndexedShutdown();\n'
            '\t if (this->hidden->SDL_primary)\n'
        )
        text = text.replace(anchor, repl, 1)

    for hook in (include_hook, bind_hook, shutdown_hook):
        if text.count(hook) != 1:
            raise RuntimeError('Round4G hook verification failed: ' + hook)

    out = text.replace('\n', nl).encode('utf-8')
    if bom:
        out = b'\xef\xbb\xbf' + out
    return out


def main():
    if len(sys.argv) != 2:
        raise SystemExit('Usage: python apply_round4g_liujiny.py /path/to/Salvia')

    repo = Path(sys.argv[1]).resolve()
    if not (repo / 'src' / 'salvia.cpp').exists():
        raise SystemExit(f'Not a Salvia repository root: {repo}')

    # Preflight is intentionally all-or-nothing. No file is touched before this passes.
    status = {}
    errors = []
    payloads = list(COPY_FROM.values()) + [
        'sdl/libs/libSDLx360/SDL/src/video/xbox/SDL_xbox_mamepalette.inc'
    ]
    for pkgrel in payloads:
        if not (PKG / pkgrel).is_file():
            errors.append(f'missing installer payload: {pkgrel}')

    for rel, allowed in ALLOWED.items():
        p = repo / rel
        if not p.exists():
            errors.append(f'missing: {rel}')
            continue
        h = nhash(p)
        status[rel] = h
        if h not in allowed:
            errors.append(f'unknown revision: {rel}\n  sha256(normalized)={h}')

    sdl = repo / SDL_REL
    if not sdl.exists():
        errors.append(f'missing Xbox SDL driver: {SDL_REL}')
        patched_sdl = None
    else:
        try:
            patched_sdl = patch_sdl_text(sdl.read_bytes())
        except Exception as e:
            errors.append(str(e))
            patched_sdl = None

    if errors:
        print('Round4G liujiny-base PRECHECK FAILED. Nothing was changed.\n')
        for e in errors:
            print(' -', e)
        print('\nUse the exact liujiny working baseline from this project, or send the current full tree for a rebase.')
        raise SystemExit(2)

    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    backup = repo / '.round4g_liujiny_backup' / stamp
    touched = list(COPY_FROM.keys()) + [SDL_REL]
    if (repo / INC_REL).exists():
        touched.append(INC_REL)

    for rel in touched:
        src = repo / rel
        if src.exists():
            dst = backup / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)

    for rel, pkgrel in COPY_FROM.items():
        dst = repo / rel
        write_package_text_preserve_style(PKG / pkgrel, dst)
        print('installed', rel)

    inc_dst = repo / INC_REL
    inc_dst.parent.mkdir(parents=True, exist_ok=True)
    write_package_text_preserve_style(
        PKG / 'sdl/libs/libSDLx360/SDL/src/video/xbox/SDL_xbox_mamepalette.inc',
        inc_dst
    )
    print('installed', INC_REL)

    sdl.write_bytes(patched_sdl)
    print('patched  ', SDL_REL)

    bad = []
    for rel, target in TARGET_HASH.items():
        got = nhash(repo / rel)
        if got != target:
            bad.append((rel, got, target))

    sdl_text = normalized_bytes(sdl).decode('utf-8')
    for token in [
        '#include "SDL_xbox_mamepalette.inc"',
        'XBOX_MameIndexedBindForMainQuad();',
        'XBOX_MameIndexedShutdown();'
    ]:
        count = sdl_text.count(token)
        if count != 1:
            bad.append((SDL_REL, f'{token} count={count}', 'exactly once'))

    manifest = {
        'package': 'Round4G LiujinyBase',
        'backup': str(backup),
        'pre_install_hashes': status,
        'verify_errors': bad,
    }
    (repo / 'ROUND4G_LIUJINY_APPLIED.json').write_text(
        json.dumps(manifest, indent=2), encoding='utf-8'
    )

    if bad:
        print('\nInstall completed but verification found problems:')
        for b in bad:
            print(' ', b)
        print('Backup:', backup)
        raise SystemExit(3)

    print('\nRound4G liujiny-base applied successfully.')
    print('Backup:', backup)
    print('Next: review `git diff`, then build with your Xbox 360 XDK toolchain.')


if __name__ == '__main__':
    main()
