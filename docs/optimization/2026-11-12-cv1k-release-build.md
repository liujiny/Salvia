# CV1000: Release 版本构建与镜像记录

日期：2026-11-12
基线 Commit：`4478715a92f57b1a2238594ca0bef4d07e774be9`
构建 Commit：`618dca82f54efaf14fcdcee82e5e1340037cf471`

## 1. 编译问题修复

在 `SALVIA_FBNEO_DIAGNOSTICS 0`（Release）模式下首次编译时，`libretro/FBNeo/src/burn/drv/cave/d_cv1k.cpp` 中 `DrvFrame` 尾部的诊断统计代码块使用了运行时 `if (SALVIA_FBNEO_DIAGNOSTICS)`，而所引用的 `wait_start`、`wait_end`、`interpBefore`、`StartupDetail`、`s_startup_stalls`、`s_startup_stall_count` 均受 `#if SALVIA_FBNEO_DIAGNOSTICS` 预处理保护，导致编译报未声明标识符错误。

在 `618dca82` 中将该代码块统一改为 `#if defined(_XBOX) && SALVIA_FBNEO_DIAGNOSTICS` 预处理保护（不改变诊断模式行为，仅使 Release 模式可正常裁剪编译）。

## 2. 构建命令与产物标识

构建命令：
```bash
/home/humor/salvia-tests/xex-build/build-xex.sh --tag cv1k-release --flavor release --primary release
```

构建产物（`SALVIA_FBNEO_DIAGNOSTICS 0`，已部署至 `Distro360/` 并外部归档）：

| 镜像 | 字节数 | SHA-256 | 外部归档路径 |
| --- | ---: | --- | --- |
| `Distro360/fbneo.xex` | 34,832,384 | `fe2df98f22f17d18c6cc62c75c53f65aefcfd936a7255bce7520790c5ed31dd4` | `/home/humor/salvia-tests/xex-archive/fbneo-20261112-2007-cv1k-release-release.xex` |
| `Distro360/mmpork.xex` | 34,832,384 | `bdb17180df3a6dd0e4b70702dcb30557f550d78868d81ee7750dc5964416c7db` | `/home/humor/salvia-tests/xex-archive/mmpork-20261112-2009-cv1k-release-release.xex` |
| `Distro360/ddpsdoj.xex` | 34,832,384 | `b84c106764c261f54926f5515e2de65dfaf2939e3e9f4ef3301474a91aa1982c` | `/home/humor/salvia-tests/xex-archive/ddpsdoj-20261112-2011-cv1k-release-release.xex` |

## 3. 验证边界

- 已验证 Xbox 360 VS2010/XDK Release（core + frontend）全量编译链接通过，并验证输出 XEX 校验和与构建清单 `/home/humor/salvia-tests/xex-build/artifact-cv1k-release.json` 一致。
- 本轮未自动覆盖实机上传或实机游玩测试，实机请以部署后实际运行为准。
