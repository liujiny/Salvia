# danibiker/Salvia v1.0.9 merge report

The integration worktree was updated to upstream `danibiker/main` (v1.0.9),
then the verified Xbox 360 Round4G/MAME changes were overlaid. The merge was
checked by rebuilding the MAME2003+ core, libSDLx360, and the Salvia MAME
frontend. The FBNeo Xbox 360 core and frontend were also rebuilt.

## Compatibility assessment

- Upstream's adaptive frame pacing (`src/io/sync.cpp` and `sync.h`) was kept;
  it replaces the older fixed-tail limiter and does not add a framebuffer queue.
- Round4G indexed-palette/direct-framebuffer code remains present, with the
  original fallback path and compile-time experimental CRT/ring options off.
- KI/KI2 fastmem regions remain unchanged; no new I/O mapping or spin hack was
  added.
- Audio worker and other frontend changes from upstream were retained. No Xbox
  360 hardware validation was performed in this environment.

## Builds

Successful local outputs:

- `libretro/mame-2003-plus/build/Xbox 360/Release/mame2003_plus_libretro_xdk360.lib`
- `libretro/FBNeo/projectfiles/visualstudio-2010-libretro-360/Release/Xbox 360/libretro.lib`
- `build/Release_mame/Xbox 360/mame-2003-plus.xex`
- `build/Release_finalburn/Xbox 360/fbneo.xex`

The frontend builds report only `xbecopy X1001` after `ImageXex`, because no
development Xbox was connected. The XEX files were produced locally.

## XEX size and compression

The generated XEX files are approximately 31.2 MiB (MAME2003+) and 34.4 MiB
(FBNeo). Xbox XEX compression is LZX/XEX-specific and must be performed with
Microsoft/Xbox `xextool.exe` (`xextool -c c file.xex`), not ZIP or 7-Zip.
The repository contains only a download note for xextool; the executable was
not available in this environment, so no compressed binary was substituted.

