# CV1000 startup RAM check

On Xbox 360, `dfkbl`, `ddpdfk` and `ddpsdoj` expose **Skip Startup RAM Test
(restart)** under the game's DIP switches. It defaults to **On** and applies to
the next cold start or reset. Off restores the original startup on the next
reset. Loading an existing state preserves that state's program and does not
apply a pending boot patch.

The game first copies/decompresses its executable into main RAM. Once the
verified startup RAM-check callback and its caller are present, the driver
replaces only the callback's first six SH3 instructions. It reports completion
and clears the RAM-check display timer. The original callback repeatedly reads
code RAM and tests/restores free RAM with alternating patterns; it is not the
program loader or required initialization.

The patch is restricted by set name and program ROM CRC, then checks the
callback's prologue, a 16-instruction caller signature and its destination
literal. Any mismatch leaves the original code intact. Checking stops after
application or after 960 frames. Only these three sets expose the option.

| Set | Program CRC | RAM callback | Layout |
| --- | --- | --- | --- |
| dfkbl | 8092ca9d | 0c2314f0 | DFK |
| ddpdfk | 9976d699 | 0c22fe20 | DFK |
| ddpsdoj | e2a4411c | 0c21e850 | SDOJ |

ROM archives, decompression, initial RAM clearing, game/graphics loading and
service-menu diagnostics are preserved. First-use EEPROM initialization still
runs; its contents must be created normally. This also does not disable the
separate, once-per-device GPU renderer validation.

## Verification

The private big-endian PowerPC harness runs each set for 1,800 frames from cold
boot, through startup and into gameplay. With the option Off, 240 startup frames
have identical per-frame video/audio hashes and complete final state to the
unmodified startup. Guard tests use each actual decompressed program and check
the exact six-instruction write range, disabled mode, corrupt prologue/caller/
literal, wrong CRC and an unrelated set. ROMs and states are not distributed.

The implementation is included for Xbox and the `SH3_PPC_DRC_TEST` test build.
Interpreter and dynamic recompiler execute the same replacement instructions;
no host CPU registers or emulated clock rates are changed.
