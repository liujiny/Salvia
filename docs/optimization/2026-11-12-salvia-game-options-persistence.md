# Salvia 前端：修复按游戏核心配置（`.opt`）无法保存及加载 DIP 开关等游戏级选项

日期：2026-11-12
基线 Commit：`b556db17c42d1a02c5f9b5332d77b6f47717488a`
修复 Commit：`402d87f26f98b1a04865004f66f8cf36b5c3d0c8`
变更文件：
- `src/io/cfgloader.h`
- `src/io/cfgloader.cpp`
- `src/salvia.h`
- `src/salvia.cpp`

## 1. 根因分析

1. **保存遗漏 `gameSpecificLibretroParams`**：
   - `RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2` 将 `gameCategories`（`dipswitch`、`cheat`、`ips`、`romdata`）分类下的核心选项放入 `CfgLoader::gameSpecificLibretroParams`，而非 `startupLibretroParams`。
   - `CfgLoader::saveGameCoreParams` 此前只遍历 `startupLibretroParams` 写入 `<游戏名>.opt`，导致所有 DIP 开关（包括 `Timing Model (ibara360)`）根本未写入磁盘。
2. **加载时与 `SET_CORE_OPTIONS_V2` 脱节**：
   - `launchGame` 在 `retro_load_game` 之前调用 `loadCoreParamsForGame`，`applyCoreParamsFile` 将尚未被核心声明的键临时写入 `startupLibretroParams`。
   - 随后 FBNeo 在 `retro_load_game` 内触发 `SET_CORE_OPTIONS_V2`，清空 `gameSpecificLibretroParams` 并将 `dipswitch` 选项新建于 `gameSpecificLibretroParams`，未读取 `startupLibretroParams` 中的已加载选择，直接回退为 `defaultIdx`；随后的 `GET_VARIABLE` 先命中 `startupLibretroParams` 中的空壳条目返回 `false`，再回退到 `gameSpecificLibretroParams` 取回默认值。

## 2. 修复内容

1. 在 `CfgLoader` 新增 `pendingLibretroSelections`（`std::map<std::string, int>`），用于暂存 `.opt` 中在 `SET_CORE_OPTIONS*` 之前加载但尚未被核心声明的选项索引。
2. 抽取 `appendCoreParamsLines` 与 `resetParamsMapToDefaults` 复用逻辑：
   - `saveGameCoreParams` 先导出 `startupLibretroParams`（跳过存在于 `gameSpecificLibretroParams` 的重复键），再导出 `gameSpecificLibretroParams`，将全部游戏级选项一并持久化到 `<游戏名>.opt`。
   - `resetCoreParamsToDefaults` 重置并清空 `pendingLibretroSelections` 与上一局遗留的 `gameSpecificLibretroParams`，防止跨游戏串扰。
3. 更新 `applyEntry` 与 `processParameters`（覆盖 V0/V1/V2 核心选项注册路径），在创建或更新选项条目时消费并应用 `pendingLibretroSelections` 中对应键的已保存索引（同时做边界校验），并新增 `preserveGameSpecificBeforeClear` 以在单局内多次触发 `SET_CORE_OPTIONS_V2`/`SET_VARIABLES` 时保留实时修改。

## 3. 构建与部署记录

构建命令（Release 分支，`SALVIA_FBNEO_DIAGNOSTICS 0`）：
```bash
/home/humor/salvia-tests/xex-build/build-xex.sh --tag cv1k-game-opt-fix --flavor release --primary release
```

构建产物与归档（基于 `402d87f2`）：

| 镜像 | 字节数 | SHA-256 | 外部归档路径 |
| --- | ---: | --- | --- |
| `Distro360/fbneo.xex` | 34,832,384 | `fc177d5cecfb426346978729064d89e35869c62bd244c776761cec17c0486459` | `/home/humor/salvia-tests/xex-archive/fbneo-20261112-2104-cv1k-game-opt-fix-release.xex` |
| `Distro360/mmpork.xex` | 34,832,384 | `0acfc045d5786f14ee64f85f4529c8ef7cf12291be50395ad6aefb1949418677` | `/home/humor/salvia-tests/xex-archive/mmpork-20261112-2106-cv1k-game-opt-fix-release.xex` |
| `Distro360/ddpsdoj.xex` | 34,832,384 | `018023adf8c3e68bc2ba02a0885316bd55d0e249f4f687676341d9afd02f0f0a` | `/home/humor/salvia-tests/xex-archive/ddpsdoj-20261112-2108-cv1k-game-opt-fix-release.xex` |
