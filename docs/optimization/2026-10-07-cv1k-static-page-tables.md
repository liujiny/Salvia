# CV1000：把 TMS34010 / Hyperstone 页表从静态镜像搬到堆上

Baseline SHA for this round: `e77da160`.

Companion documents:
[arena size](./2026-10-07-cv1k-arena-size.md),
[arena accounting](./2026-10-07-cv1k-arena-accounting.md),
[GPU init failure probe](./2026-10-07-cv1k-gpu-init-failure-probe.md).

## 现象

实机载入 `ddpsdoj` / `ddpdfk` / `ibara` 后横幅固定显示
`cv1000 gpu:cpu fallback - initialization failed`，即 Xenos 合成器初始化失败，
全部帧退回 CPU 渲染。`Distro360/cv1000-gpu.log` 只记录了第一处失败：

```
CV1000 GPU: INIT FAILED step=atlas texture hr=0x8007000E free=6MB->4MB ...
```

`0x8007000E` 是 `E_OUTOFMEMORY`，失败点是
`epic12_xenos_create_atlas()`：一张 2048 x `capacity*8` 的
`D3DPOOL_DEFAULT` 纹理，256 槽合 16 MiB、128 槽合 8 MiB。进入游戏时只剩
6 MB，两张都建不起来，于是 128 槽兜底也失败并整体回退。

## 为什么静态数组会吃掉这块内存

`epic12_xenos_free_mb()` 用的是 `GlobalMemoryStatus()` 的 `dwAvailPhys`，也就是
标题可用的物理页。Xenos 的 `D3DPOOL_DEFAULT` 资源同样来自这块物理池。因此
PE 镜像里的 `.bss` 与合成器要申请的 atlas 是**同一笔预算**：静态数组占住的
就不是"镜像预算"，而是合成器拿不到的那部分。

`Salvia.map` 显示改前 `.bss = 0x04d6bc7c`（77.4 MiB）。用正确的小端 COFF
解析（`machine = 0x01F2`）逐目标文件统计 `libretro.lib` 中 >= 256 KiB 的
`.bss/.data` 数组后，与 CV1000 无关的两处最大：

| 目标文件 | 大小 | 数组 | 说明 |
| --- | ---: | --- | --- |
| `tms34_intf.obj` | 32.00 MiB | `MapStore[MAX_CPUS]` | 4 槽 x `UINT8 *map[PAGE_COUNT*2]` |
| `e132xs.obj` | 8.02 MiB | `mem[2][0x100000]` | Hyperstone 两张页表 |

两者都只服务于 CV1000 用不到的 CPU：TMS34010/TMS34020 只在 Midway 系驱动，
Hyperstone 只在 `vamphalf` 等驱动。`sh4.obj` 里那个 32.85 MiB 的数组是
SH3 PPC DRC 的 `xbox_code`，属于本项目本体，不能动。

`.data` 里的 `Pgm2VideoRegs`、`TC0180VCURAM`、`konami_priority_bitmap` 都是
`BurnMalloc` 出来的堆指针，不是静态数组——上一轮把它们记为静态占用是错的，
这里一并更正。

## 改动

- `tms34_intf.cpp`：`TMS34010MemoryMap::map` 由内联数组改为指针，改由
  `TMS34010MapStoreAlloc()` 在 `TMS34010Open()` 首次打开该槽时
  `BurnMalloc` 并清零；`TMS34010Init_Internal()` 里原来的整块
  `memset(&MapStore, 0, sizeof(MapStore))` 换成 `TMS34010MapStoreClear()`，
  只清状态、保留已持有的页表；`TMS34010Exit()` 追加
  `TMS34010MapStoreRelease()` 释放。
- `e132xs.cpp`：`mem[2][0x100000]` 改为 `UINT8 **mem[2]`，新增
  `e132xs_ensure_map()` 首次使用时分配并清零，`E132XSMapMemory()` 与
  `E132XSInit()` 都调用它；原本空实现的 `E132XSExit()` 负责释放。
- 两者都保留原来的语义：条目内容仍是清零后由各驱动的 map 函数填充，页表
  地址不变，读/写快速路径代码一行未改。

## 为什么不是把堆调大

`Salvia.vcxproj` 的四处 `<HeapSize>0x18000000</HeapSize>`（384 MiB）经 XEX2
头的 `0x20401`（Default Heap Size）键核对，和上游官方发布的
`fbneo.xex` / `fbanext.xex` / `mame-2003-plus.xex` 完全一致。堆已经不是瓶颈，
再调大只是把同一笔物理内存换个名字，所以本轮的修法是减少静态占用。

## 构建结果

`scripts/msbuild.cmd`（`fba_vs2010_libretro_360.sln` Release +
`Salvia.vcxproj` `Release_finalburn`）：**PASS**，三个被改文件均在编译清单里
（`tms34_intf.cpp`、`e132xs.cpp`、`sh4.cpp`），`Salvia.map` 中出现新增的
`TMS34010Open(%d); out of memory for the page table.` 字符串，确认改动进了镜像。

`Salvia.map` section 表：

| Section | 改前 | 改后 | 变化 |
| --- | ---: | ---: | ---: |
| `.text` | `0x0153f224` | `0x0153f224` | 0 |
| `.data` | `0x006f60c8` | `0x006f60c8` | 0 |
| `.bss` | `0x04d6bc7c`（77.4 MiB） | `0x0196bd7c`（25.4 MiB） | **-52.0 MiB** |

-52.0 MiB 与"页表 40 MiB + 本轮 arena 20 MiB 化的 12 MiB"吻合（arena 见配套
文档）。按实机日志进 atlas 前只剩 6 MB 推算，新构建进同一路径应约有 58 MB，
16 MiB 的 256 槽 atlas 有足够余量。

产物：`Distro360/fbneo.xex` 34,598,912 字节，SHA-256
`45abb372ef250f9ee1ca383d8cd67e81f3643a8ca28490ab1ae77fd96bdc1270`。

## 验证边界

- 只有宿主编译与 map/COFF 静态核账。**没有任何实机 FPS 或帧时间结论**。
- TMS34010 与 Hyperstone 驱动的运行路径未在实机上跑过：Midway /
  `vamphalf` 系 ROM 需要回归一次，确认懒分配后页表填得正确。
- XEX 尚未在主机上确认是否脱离回退，也未确认 `back+Y` 菜单崩溃是否随之消失。

## 状态

已定位、已改、已构建；等待实机确认。
