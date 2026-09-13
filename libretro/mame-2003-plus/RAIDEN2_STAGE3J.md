# Stage3j source-only Raiden II save-state fix

No compilation was run, per user request.

Root cause of an empty Salvia savegames list: driver.h defined
GAME_DOESNT_SERIALIZE as 0x0420. That is not an independent flag; it overlaps
GAME_IMPERFECT_SOUND (0x0400) and GAME_WRONG_COLORS (0x0020). Raiden II has
GAME_IMPERFECT_SOUND, so state_get_dump_size() treated it as nonserializable,
returned zero, and the frontend never queued or wrote a snapshot.

GAME_DOESNT_SERIALIZE is now the unused independent bit 0x0800. Existing game
flags retain their meanings. No driver in this tree explicitly uses
GAME_DOESNT_SERIALIZE, so this corrects accidental rejection without removing
an intentional per-driver serialization block.

Generating a file alone was insufficient for the Stage2/3 backport. Register
the Raiden II video/bank state, complete COP register/DMA/sort/collision state,
and Seibu command/reply pending state, IRQ-vector state and sound ROM bank.
Mapped CPU, sprite, tile, palette and ordinary RAM are already registered by
MAME's memory system; CPU, YM2151 and OKI state use their device registrations.

Postload restores BANK3/BANK4 from the saved program-bank value, restores the
Seibu sound bank and IRQ line, and invalidates all tilemaps so saved bank and
RAM contents are redrawn. Immutable copied/decrypted ROM/GFX data is not saved.
All multi-byte emulated state uses typed MAME registration, preserving the
existing MSB_FIRST conversion behavior instead of serializing host padding.
Collision structures are registered field-by-field for that reason.

Modified files:
- src/driver.h
- src/drivers/raiden2.c
- src/sndhrdw/seibu.c
- src/sndhrdw/seibu.h
- this document

Static checks: GAME_DOESNT_SERIALIZE no longer overlaps any existing game flag;
all state callbacks have stable storage and explicit declarations; mapped RAM
is not duplicated by the driver registrations; git diff --check passes.
Compiler, link and Xbox runtime validation remain pending.

Manual build order for the current source tree remains:
1. libs/libSDLx360/libSDLx360.sln — Release|Xbox 360
2. libretro/mame-2003-plus/mame2003_plus_libretro.sln — Release|Xbox 360
3. Salvia.sln — Release_mame|Xbox 360

The SDL build is needed to remove the prior Stage3h binary even though this
save-state fix itself changes only core files. Test a new slot during stage 1,
confirm the list is populated, move/fire for several seconds, load it, then
verify position, enemies, scrolling, sprites, music/effects and coin/start.
Also make a second snapshot after a program-bank transition and cold-restart
Salvia before loading it. Old states made before this registry change are not
signature-compatible and should not be used.
