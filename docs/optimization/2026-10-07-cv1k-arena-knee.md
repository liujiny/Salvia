# CV1000：把 SH3 PPC 代码 arena 收到实测 knee（20 MiB）

Baseline SHA for this round: `e77da160`.

Companion documents:
[arena size](./2026-10-07-cv1k-arena-size.md),
[block table size](./2026-10-07-cv1k-block-table-size.md),
[static page tables](./2026-10-07-cv1k-static-page-tables.md).

## 为什么在已经定到 32 MiB 之后还要改

上一轮把 `SH3_PPC_CACHE_BYTES` 从 8 MiB 提到 32 MiB，依据是 8 MiB 会打满并
触发 reset。那一轮只测了 8 / 32 / 64 MiB 三个点，32 MiB 是用"32 与 64 表现
相同"外推出来的，中间没有测过。

本轮实机暴露出真正的瓶颈不是 DRC 而是物理内存：Xenos 合成器在进入游戏时
只剩 6 MB，连 8 MiB 的兜底 atlas 都建不出来。arena 在 32 位 Xbox 路径上是
静态 `xbox_code[]`，纯 `.bss`，与其让 32 MiB 里 14 MiB 永远用不到，不如把
它收到实测 knee，把这部分直接让给 atlas。

## 扫频证据

`qemu-ppc -cpu g4`，1800 帧，块表固定在本轮的 131072 项，从 `Distro360/`
里随包的中途存档起跑：

| Guest | arena | recycles | peak | rebuilds |
| --- | ---: | ---: | ---: | ---: |
| `ddpsdoj` | 16 MiB | - | - | 58,511 |
| `ddpsdoj` | 20 MiB | 0 | 18,779,744 | 43,214 |
| `ddpsdoj` | 24 MiB | 0 | 18,779,744 | 43,214 |
| `ddpsdoj` | 32 MiB | 0 | 18,779,744 | 43,214 |
| `ddpsdoj` | 64 MiB | 0 | 18,779,744 | 43,214 |

20 / 24 / 32 / 64 MiB 四个点的 recycle 数、high-water（17.9 MiB）与 rebuild
数逐位相同；16 MiB 已经回退到 58,511 次 rebuild。knee 落在 16 与 20 MiB
之间，因此取 20 MiB，相对 32 MiB 省下 12 MiB 静态占用而不改变 DRC 行为。

## 改动

`sh3_drc_ppc.h` 的默认值 `SH3_PPC_CACHE_BYTES` 由 32 MiB 改为 20 MiB，并把
宿主扫频结论写进注释，避免以后又凭猜测调回。`SH3_PPC_CACHE_BYTES` 覆盖宏
保留，宿主差分测试仍可从同一份源码复现旧尺寸。

## 验证边界

- 扫频是宿主 `qemu-ppc` 证据，不是实机结论。改动对 360 的收益（释放 12 MiB
  物理页）由 `Salvia.map` 的 `.bss` 差值佐证，见配套文档。
- 没有实机 FPS 或帧时间数字。

## 状态

已扫频、已构建；实机确认待做。
