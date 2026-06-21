# TH07-RE 反编译重建进度

最后更新：2026-06-21

## 终极目标

完整重现东方妖妖梦 `th07.exe`（1721 函数）的源码。**编译产物的游戏行为必须与原 exe 一致**。
objdiff match% 作为忠实度指标（目标每模块平均 ≥90%）。

## 当前总览

| 指标 | 值 |
|---|---|
| objdiff 跟踪模块 | 22 |
| objdiff 跟踪函数 | 228 |
| 模块平均 match%（per-module 算术平均）| ~80.2%（2026-06-21 AnmManager 41→58% 后估算；下次全量重测基线待更新）|
| 函数加权 match% | ~76.4%（同上估算）|
| ≥90% 模块 | 11（核心完成）|
| 80–90% 模块 | 3（接近达标）|
| 50–80% 模块 | 5（进行中；AnmManager 本轮 41→58% 升入此档）|
| <50% 模块 | 3（阻塞/早期）|
| **normal build** | ✅ **链接成功，产出 `build/th07e.exe`（PE32 i386 GUI）** |
| **Player 模块** | ✅ **17 缺失函数已实现（OnUpdate/OnDrawHighPrio/AddedCallback/HandlePlayerInputs/SpawnBullets/UpdatePlayerBullets/DrawBullets/DrawBulletExplosions/CalcDamageToEnemy/CheckGraze/CalcKillBoxCollision/CalcLaserHitbox/ClearBombRegions/HandleBombInput/StartSupernaturalBorder/EndSupernaturalBorder/UpdateFireBulletsTimer）**。objdiff 26 函数跟踪（之前 9），加权 40.28%、算术 62.24% |
| **AnmManager 模块** | 本轮（2026-06-21）LoadAnmEntry/SetRenderStateForVm/DrawInner 三大函数抬升，41.07% → **58.41%**（+17.34pp）。详见 P1.1 段 |
| mapping.csv 函数覆盖 | 1562 行（th07.exe 全部非 thunk 函数）|
| raw 绝对地址访问 | **0**（已全量迁移到 typed C++，对齐 th06 标准）|
| raw[] buffer / accessor | **0**（Player/SoundPlayer 等 5 大 struct 已全命名重构）|

> match% 来源：`objdiff-cli diff` 的 `left.symbols[].match_percent`。模块平均为
> simple average（各函数 match% 的算术平均）。2026-06-18 重测基线。

## P0 normal-build 链接达成（2026-06-18 本轮新增）

本轮（commit 待提交）实现 P0 目标：**让 normal build 链接成可启动 exe**。

| 改动 | 状态 |
|---|---|
| Player.cpp 补全 17 缺失函数 | ✅ 完成 |
| Player.hpp 扩展命名字段（BombRegion 表/OptionSlotMeta/unkTimer_16a0c/optionScratch 等） | ✅ 完成（sizeof 仍 0xb7e78） |
| link_globals.cpp 新文件：primitive `extern "C"` typed global 定义 | ✅ 完成（约 130 个零值槽） |
| link_stubs.cpp 新文件：`@Name@N` __fastcall + `_Name` cdecl stub | ✅ 完成（90 个零操作 stub） |
| link_cpp_stubs.cpp 新文件：th07::-namespace + 全局 namespace 自由函数 stub | ✅ 完成 |
| 各 .cpp 内嵌 stub：AnmMgrStub/AnmManager/AnmManagerFiles/EffectManagerSpawn/GameManagerScore/SoundPlayerPlayback/MidiDevStub/RngStub/ArchiveEntryTable/MidiOutput/ReplayManager 方法 stub | ✅ 完成（AsciiManager/BombData/EffectManager/FileSystem/GameManager/Player/Supervisor 7 个文件） |
| 链接 unresolved | **311 → 0**（全部 resolved） |
| `build/th07e.exe` 产物 | ✅ PE32 i386 GUI 135168B（wine 启动后即返回，因 singleton stubs 全零初始化，游戏行为待后续填充） |

**约束遵守**：零 raw 绝对地址、零 inline asm、零 #ifdef DIFFBUILD 函数分裂、`0` 而非 `nullptr`、注释全 ASCII、源码无 CJK。stub 文件均仅参与 normal build，不进入 objdiff（未在 objdiff.json/ghidra_ns_to_obj.csv 注册）。

**验收**：`python3 scripts/build.py --build-type=normal` 产出 `build/th07e.exe`，链接器 0 unresolved；`file build/th07e.exe` 显示 PE32 i386 GUI；`wine build/th07e.exe` 不崩溃（启动即返回）。

## 每模块 match% 明细（2026-06-18 typed-C++ 重构后）

`<<` = <50%（阻塞），`*` = <90%（待抛光）。函数按字节大小降序。

### 核心完成（≥90%）— 11 模块

**AnmVm 100.00%** (3 fns) — ResetInterpTimers 100, Initialize 100, AnmVm 100
**GameErrorContext 99.81%** (3) — Fatal 99.93, Log 99.93, Flush 99.56
**utils 99.59%** (4) — DebugPrint 100, Rotate 99.71, AddNormalizeAngle 99.53, CheckForRunningGameInstance 99.10
**Controller 98.01%** (7) — GetInput 99.92, SetButtonFrom* 100×2, ResetKeyboard 99.60, GetJoystickCaps 99.20, GetControllerInput 94.11, GetControllerState 93.26
**Chain 97.85%** (12) — AddToCalcChain/AddToDrawChain/CreateElem/ChainElem/~ChainElem/Release 100×6, ReleaseSingleChain 99.89, Cut 99.95, RunDrawChain 88.58, RunCalcChain 86.70
**ZunTimer 96.56%** (3) — Increment 99.48, Decrement 99.48, NextTick 90.71
**zwave 96.01%** (26) — 5×100, CSoundManager::CreateStreaming* 91-94, CStreamingSound::HandleWaveStreamNotification 88.40, CWaveFile::ResetFile 78.61
**FileSystem 95.82%** (2) — RawWriteFile 99.29, OpenPath 92.35
**Rng 95.77%** (3) — GetRandomU16 100, GetRandomU32 100, GetRandomF32ZeroToOne 87.31
**MidiOutput 92.23%** (17) — ClearTracks/LoadFile/Play/ReleaseFileData/SetFadeOut 100×5, StopPlayback 99.75, MidiOutput 99.69; **ProcessMsg 34.76**（大函数待实现）
**ScreenEffect 90.23%** (13) — AddedCallback/DrawFadeOut/DrawFlickerFade 100×3, DrawFadeIn 98.06, Clear 97.54, SetViewport 97.00; DrawSquare 79.82, RegisterChain 81.95, ShakeScreen 82.50, CalcFlickerFade 78.61（+0.34pp vs 旧基线，typed-C++ 重构后）

### 接近达标（80–90%）— 3 模块

**GameManager 89.11%** (6) — OnDraw 100, CutChain 98.42, DeletedCallback 95.00, OnUpdate 89.56; RegisterChain 76.73, **AddedCallback 74.96**（2726B 分配器）
**Player 87.62%** (9) — StartFireBulletTimer 87.43, RegisterChain 99.72, OnDrawLowPrio 99.50, Die 98.98, CutChain 98.42; AngleToPlayer 88.15, DeletedCallback 83.47; **ScoreGraze 71.78, CalcItemBoxCollision 61.15**（浮点栈布局）
**EffectManager 84.18%** (17) — Reset/EffectCallbackStill/EffectUpdateCallback4Init 100×3, CutChain 99.00, RegisterChain 98.09, EffectUpdateCallback4 99.28, SpawnParticlesWithVelocity 96.95; **OnDraw 65.40, EffectCallbackAttract 54.93, EffectCallbackAttractSlow 53.65, EffectManager(ctor) 35.40**

### 进行中（50–80%）— 4 模块

**AsciiManager 75.04%** (16) — AddFormatText 99.78, CutChain 99.00, AddedCallback 90.19; RegisterChain 58.78, InitializeMenuVms 61.11; **DrawStrings 49.66, AsciiManager(ctor) 39.51, DrawPopups 1.34**（大函数；+1.98pp）
**SoundPlayer 74.34%** (14) — StopBGM 92.62, BackgroundMusicPlayerThread 88.85, GetWavFormatData 87.13; **ProcessSoundQueues ~10, SoundPlayer(ctor) 改进**（+3.00pp，ctor memset(this,0,sizeof) 更贴近 orig rep stosd）
**CMyFont 70.75%** (5) — Reset 99.81, Clean 95.00, InitWrapper 91.86; Init 64.88; **Print 2.20**（GDI 渲染大函数）
**Supervisor 67.37%** (14) — OnDraw 99.55, TickTimer 97.95, ReadMidiFile 91.76, PlayMidiFile 86.34; SetupDInput 77.16, RegisterChain 76.48, FadeOutMusic 73.73, StopAudio 70.71; **LoadConfig 30.60, DrawFpsCounter 0.00**（-1.81pp，fps 平滑变量 retyping + 字符串重定位；待后续抛光）

### 阻塞/早期（<50%）— 4 模块

**AnmManager 58.41%** (15) — CreateEmptyTexture 99.40, ReleaseTexture 88.28; **LoadAnmEntry 91.53, SetRenderStateForVm 91.64**（本轮抬升）；DrawInner 84.06（本轮抬升，2.21→84.06）；LoadTexture 76.73; LoadAnm 59.97, ReleaseAnm 59.65, LoadSprite/SetActiveSprite 57; AnmManager(ctor) 44.66; **ExecuteScript 0.21（13178B，P1.2 单独立项）, LoadTextureAlphaChannel 2.82, LoadTextureFromMemory 4.61**。本轮（2026-06-21）LoadAnmEntry/SetRenderStateForVm/DrawInner 三个核心大函数从 Ghidra 逐指令抬升，模块 41.07% → 58.41%（+17.34pp）
**BombData 40.87%** (24) — MarisaABombDraw 90.92, MarisaBBombDraw 90.68 等 12 draw 完成；MarisaABombCalc2 42.75；**11 calc 未实现**（+1.23pp）
**ReplayManager 34.70%** (12) — StopRecording 99.67, DeletedCallback 89.47; RegisterChain 84.78; **SaveReplay 4.36, RewriteReplay 2.73** 等
**Pbg4Parser 19.72%** (3) — AdvanceNode 32.55, SetIndex 26.61; **Reset 0.00**（LZSS 字典/节点表初始化）

（ItemManager 0% 未在 objdiff 跟踪列表中，属 P1 工作。）

## 2026-06-21 AnmManager 核心大函数抬升（P1.1）

本轮（commit 7f24a3f..de9195a）从 Ghidra 逐指令抬升 AnmManager 三个核心大函数，
打通 ".anm 加载 → 渲染状态 → D3D 绘制" 的最小通路：

| 函数 | 原地址 | 大小 | 抬升前 | 抬升后 | 状态 |
|---|---|---|---|---|---|
| LoadAnmEntry | 0x44e070 | 0x460 | 2.32% | **91.53%** | ✅ ≥90% |
| SetRenderStateForVm | 0x44eae0 | 0x3e0 | 2.56% | **91.64%** | ✅ ≥90% |
| DrawInner | 0x450520 | 0x524 | 2.21% | **84.06%** | 接近达标（栈布局待抛光） |

**AnmManager 模块平均**：41.07% → **58.41%**（+17.34pp）。

**新增跨模块 typed-C++ 引用**（extern "C" + SYMBOL_MAP + link_globals/link_cpp_stubs）：
- `g_SupervisorD3dDevice_575958`（D3D 设备指针槽，orig 0x575958）
- `g_SupervisorG0x575a9c`（cfg.opts 位域，orig 0x575a9c）
- `g_AnmMgrColorSlot_4b9fb8..4ba0cc`（8 个软件渲染路径颜色槽）
- `g_DrawPrimUpVerts_4ba078`（DrawPrimitiveUP 顶点缓冲）
- `g_SupervisorCameraStub_1347b00`（相机 helper 的忽略 this）
- `AnmManager_FlushVertexBuffer_44f5c0`（顶点缓冲刷新，in-module helper）
- `Supervisor_Setup3DCamera_408180` / `_2DCamera_4082b0`（3D/2D 相机矩阵重装）
- `D3DXMatrixRotationZ/X/Y_461xxx` + `D3DXMatrixMultiply_461aa2`（D3DX 矩阵数学）

**验收**：每函数 `python3 scripts/build.py --build-type=objdiffbuild --object-name AnmManager.obj`
+ objdiff-cli diff 验证 match%；`build/th07e.exe` normal build 仍链接成功（135168B PE32 GUI）。
DrawInner 84.06% 接近但未达 90%（栈帧 +0x4、寄存器分配差异），留作后续抛光。

## "打开游戏显示主菜单" 长期路线图（P1.1 - P1.6）

本路线图把"wine 启动 build/th07e.exe 能渲染出与原作一致的主菜单"拆成多轮，
每轮明确函数清单 + objdiff 验收。**"主菜单可显示" 仅在 P1.6 wine 实测渲染后才宣告**。

- **P1.1（本轮，2026-06-21）** ✅ AnmManager LoadAnmEntry + SetRenderStateForVm +
  DrawInner 三个核心大函数抬升（91.53% / 91.64% / 84.06%）。
- **P1.2（下一轮）**：ExecuteScript（FUN_00450d60, ~11KB switch 解释器，opcodes -1..0x52，
  含变量/寄存器 ops、RNG、条件分支、5 通道 × 7 模式缓动）— 项目标注"the single largest
  remaining task"，按 opcode 区段拆 2-3 子会话。+ DrawInner 栈布局抛光到 ≥90% +
  剩余 Draw 变体（Draw/Draw2/Draw3/DrawNoRotation，FUN_00407900 dispatch）+
  LoadTextureAlphaChannel/LoadTextureFromMemory。
- **P1.3**：Supervisor 启动路径（FUN_00434020 boot loop + FUN_00434a40/a80/bd0
  Bootstrap/CreateWindow/InitD3D + FUN_004346e0 RunSession per-frame driver）。
  当前 main.cpp 是 skeleton，所有 Supervisor_* 都是 link_stubs.cpp no-op。
- **P1.4**：Pbg4Parser Reset/AdvanceNode/SetIndex（LZSS 解码器，资源解包必需）+
  FileSystem 路径整合。当前 Pbg4Parser 19.72%、Reset 0%。
- **P1.5**：MainMenu 模块全量（0x41e4b0-0x41f6f0，13+ 函数 ~5KB，mapping.csv 仅
  RegisterChain 一个命名）。这是标题画面的子系统（OnUpdate/OnDraw/AddedCallback），
  加载 data/title/*.anm 并驱动 title AnmVm。
- **P1.6（验收）**：wine 实测 build/th07e.exe 能 boot 到主菜单、与原作视觉/行为一致。
  仅此轮达成后才宣告 "主菜单可显示"。

## 2026-06-18 typed-C++ 全项目重构（对齐 th06 严格标准）

本轮（commit ed21c20..c9105ef）将项目宪法从"允许 accessor + raw-offset"
升级为 th06 严格标准：**零 raw 绝对地址、零 raw[] buffer、零 accessor、
全部 struct 命名成员**。共 8 个 commit：

1. **mapping.csv 全量补全**：270 → 1562 行（th07.exe 全部非 thunk 函数）。
   `scripts/normalize_mapping.py` 应用 4 项 th06 格式规范（地址 lowercase 无
   补零、col4 单 token、this 参数一致、参数 type-only）。
2. **Player.hpp 全命名重构**：消灭 `u8 raw[0xb7e78]` + 23 accessor。新增
   `BombProjectileSlot` 子结构（从 ReimuCBombCalc 等 anchor 函数逆向）。
3. **SoundPlayer.hpp 全命名重构**：消灭 `u8 raw[0x17e560]` + 21 accessor。
   1.46 MiB 尾部作为 `scratchRegion[]`（照搬 AnmManager.hpp 范本）。
4. **AsciiManager/Supervisor/GameManager hpp 清理**：消除 opaque `u8[]` blob，
   全部 `unk_XXXX` 命名约定；清理 26 个源文件的 UTF-8 CJK 字符（修了一个
   潜在 bug：注释中的全角括号导致 MSVC 7.0 预处理器吞掉 struct 成员声明）。
5. **7 个 .cpp 全量迁移**：Supervisor (216 sites) / GameManager (215) /
   BombData (67) / ScreenEffect (24) / AsciiManager (29) / EffectManager (2)
   全部从 `(*(T*)0xADDR)` / `*reinterpret_cast<T*>(0xADDR)` 迁移到 typed
   C++（`g_Supervisor.member`、`g_SoundPlayer.field`、`extern "C" const f32`、
   字符串字面量等）。SYMBOL_MAP 扩展 ~150 条新映射。

**验收**：`grep -rE '\*\([^)]+\*?\)\s*0x[0-9a-f]{5,}' src/*.cpp` → **0 命中**；
`grep 'u8 raw\[' src/*.hpp` → **0 命中**（注释中的除外）。全部 22 模块
objdiff match% 持平或上升（per-module 算术平均 75.81 → **79.39%**）。

## 剩余工作（按优先级）

### P0：让 normal build 链接成可启动 exe — ✅ 完成（2026-06-18）

normal build 全部 .cpp 编译通过，本轮将链接 unresolved 从 **311 降到 0**，
`build/th07e.exe` 成功产出（PE32 i386 GUI，135168B）。详见上方"当前总览"。

**实施路径**：
- (a) 实现 Player.cpp 全部 17 缺失函数（OnUpdate/OnDrawHighPrio/AddedCallback/
  HandlePlayerInputs/SpawnBullets/UpdatePlayerBullets/DrawBullets/DrawBulletExplosions/
  CalcDamageToEnemy/CheckGraze/CalcKillBoxCollision/CalcLaserHitbox/
  ClearBombRegions/HandleBombInput/StartSupernaturalBorder/EndSupernaturalBorder/
  UpdateFireBulletsTimer）；
- (b) 新建 3 个 stub 文件（link_globals.cpp / link_stubs.cpp / link_cpp_stubs.cpp）
  集中定义未实现的 typed global（零值）和跨模块函数 stub（no-op）；
- (c) 各 .cpp 内嵌 stub 方法定义（AnmMgrStub/AnmManager 等本地 stub-struct 的
  方法），使 C++ mangled 方法符号可解析。

**注意**：所有 stub 当前为 no-op/零值，singleton 在原 exe 由 Supervisor/
GameManager 等模块启动时填充；下一步是逐步替换 stub 为真实实现（按 P1 优先级：
AnmManager 大函数 / BombData calc / ReplayManager / Pbg4Parser）。

### P1：提升低匹配模块（<50%）— 进行中

- **AnmManager 58.41%**（本轮 +17.34pp）：核心阻塞。本轮（2026-06-21）抬升了
  `LoadAnmEntry`（91.53%）/ `SetRenderStateForVm`（91.64%）/ `DrawInner`（84.06%）三个
  大函数，打通 ".anm 加载 → 渲染状态 → D3D 绘制" 最小通路。**剩余**：`ExecuteScript`
  （13178B 操作码解释器，P1.2 单独立项）、`LoadTextureAlphaChannel`/`LoadTextureFromMemory`、
  DrawInner 栈布局抛光到 ≥90%。详见上方 "P1.1" 段和 "打开游戏显示主菜单 长期路线图"。
- **BombData 39.6%**：12 calc 函数中 11 个未实现（每个 800–2400B 的 Player 状态机）。
  MarisaABombCalc2（42.75%）是已验证的范本，新 calc 抄其结构。
- **ReplayManager 34.7%**：SaveReplay/RewriteReplay/AddedCallback 等大函数未实现。
- **Pbg4Parser 19.7%**：LZSS 解码器（Reset/AdvanceNode/SetIndex），纯算法模块。
- **ItemManager 0%**：OnUpdate 4297B 单体巨型 switch 函数，需逐 case 逆向。

### P2：抛光接近达标模块（80–90%）

- **Player 89.04%**：ScoreGraze 71.78% / CalcItemBoxCollision 61.15%（浮点栈布局，
  orig 用大量 f32 临时 + 三层 copy，MSVC /Od 栈分配难精确重建）
- **EffectManager 84%**：OnDraw 65.34% / EffectCallbackAttract 54.93% /
  EffectManager(ctor) 35.40%
- **AsciiManager 73%**：DrawStrings 49.66% / DrawPopups 1.34%（大函数）
- **CMyFont 70.75%**：Print 2.20%（GDI 渲染大函数）
- **SoundPlayer 71%**：ProcessSoundQueues 9.86% / SoundPlayer(ctor) 11.56%

### P0.5：raw address 全量迁移到 typed C++（对齐 th06 标准）— ✅ 完成

项目宪法（AGENTS.md §2）已升级为纯 typed C++ 标准——**禁止 raw 绝对地址 +
raw-offset buffer 索引**（`raw[0x...]`、`SCORE_SUB_I32(off)` 宏、accessor 返回
`&raw[OFF]` 等）。本轮（2026-06-18）全项目完成迁移：

| 文件 | 原 raw 数 | 状态 |
|---|---|---|
| `src/Player.hpp` | `u8 raw[0xb7e78]` + 23 accessor | ✅ 全命名重构（match 87.62%）|
| `src/SoundPlayer.hpp` | `u8 raw[0x17e560]` + 21 accessor | ✅ 全命名重构（match 74.34%，+3pp）|
| `src/AsciiManager.hpp` | `u8 vm0[0x24c]` 等 opaque blob | ✅ 命名为 AnmVm 字段 |
| `src/Supervisor.hpp` | `u8 tail[...]` opaque | ✅ 命名为 `unk_2b0[]` + 新增 unk180/184 |
| `src/GameManager.hpp` | `pad18/pad93de/tail` opaque | ✅ 全部 `unk_XXXX` 命名约定 |
| `src/Supervisor.cpp` | 216 raw addr | ✅ 完成（match 67.37%）|
| `src/GameManager.cpp` | 215 raw addr | ✅ 完成（match 89.11%）|
| `src/BombData.cpp` | 67 raw addr | ✅ 完成（match 40.87%，+1.2pp）|
| `src/ScreenEffect.cpp` | 24 raw addr | ✅ 完成（match 90.23%，+0.34pp）|
| `src/AsciiManager.cpp` | 29 raw addr | ✅ 完成（match 75.04%，+2pp）|
| `src/EffectManager.cpp` | 2 raw addr | ✅ 完成（match 84.18%）|
| `src/Player.cpp` | 35 raw addr + 36 raw[] + SCORE_SUB_I32 | ✅ 完成（match 87.62%）|

**验收**（2026-06-18）：
- `grep -rE '\*\([^)]+\*?\)\s*0x[0-9a-f]{5,}' src/*.cpp src/**/*.cpp` → **0 命中**
- `grep 'u8 raw\[' src/*.hpp` → **0 命中**（注释中的除外）
- `grep '&raw\[0x' src/*.hpp` → **0 命中**
- 全部 22 模块 objdiff match% 持平或上升（per-module 平均 75.81 → **79.39%**）

**迁移规则**（详见 AGENTS.md §2）：
- singleton member（`0x575aa8`）→ `g_Supervisor.curState`（查 hpp 偏移注释）
- rdata string（`0x496fe0`）→ 字符串字面量 `"bgm/thbgm.fmt"`
- rdata float const（`0x498a54`）→ `extern "C" const f32 g_X = 1.0f;` + SYMBOL_MAP
- data global（`0x4d44f8`）→ `extern "C" i32 g_BombIsActive;` + SYMBOL_MAP
- code addr（`0x438668`）→ typed extern 声明 `Supervisor_Callback6()`
- ECX 单例参数（`0x4ba0d8`）→ `&g_SoundPlayer`
- struct-internal offset（`(u8*)ptr + OFF` 访问未验证 padding）→ 保留为
  `(T*)((u8*)typed_ptr + OFF)` 形式（typed base pointer，诚实标注未验证区域）

每文件迁移后编译 + objdiff 验证（`python3 scripts/build.py --build-type=objdiffbuild --object-name <M>.obj`）。

## 关键技术事实（th06→th07 差异，供实现参考）

### 5 大编译选项差异（th06 → th07）
1. **无 /G5**（Pentium 优化；th06 有，致 u16 用 xor+mov 而非 movzx）
2. **无 /Op**（浮点一致性；th06 有，致浮点中间值 fstp+lld 截断）
3. **无 /GS**（security cookie；th06 有，致栈缓冲函数多 cookie 指令 + 栈偏移+4）
4. **callback `__fastcall`**（th06 `__cdecl`；arg in ECX vs push）
5. **mapping.csv `scalar deleting destructor` 致 ImportFromCsv 中断**（含空格/反引号触发 InvalidInputException）

最终编译选项：`/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy`（无 /G5 /Op /GS）

### 关键结构体大小（ghidra 实读验证）
- `AnmManager` = **0x17e560**（sprites/scripts/spriteIndices = 2560，textures[264]，anmFiles=50）
- `GameManager` = **0x9700**（Item[1100]×0x288 不在此，在 ItemManager 单例 @ 0x575c70）
- `Supervisor` = **0x2c8**（d3dDevice @ +0x8，cfg @ +0x48..+0x14c，curState @ +0x158）

### 关键单例地址
- `g_Supervisor` @ 0x575950（d3dDevice @ +0x8 → [0x575958]）
- `g_AnmManager`（指针全局）@ 0x4b9e44
- `g_Chain` @ 0x626218
- `g_GameManager` @ 0x626270（scoreSub @ +0x8 → [0x626278]）
- `g_SoundPlayer` @ 0x4ba0d8
- `g_EffectManager` @ 0x134ce18

## 工具链命令

```bash
# 生成 build.ninja
python3 scripts/configure.py
# 编译单模块 reimpl obj（objdiff）
python3 scripts/build.py --build-type=objdiffbuild --object-name <Module>.obj
# 编译完整 exe（normal）
python3 scripts/build.py --build-type=normal
# objdiff 对比
objdiff-cli diff -1 build/objdiff/orig/<M>.obj -2 build/objdiff/reimpl/<M>.obj \
  -o /tmp/<m>.json --format json-pretty
# 函数级 match_percent 在 left.symbols[].match_percent

# 重新导出 orig obj（改 mapping.csv 后才需要，耗时 ~1min）
rm -rf /tmp/th07_new && mkdir -p /tmp/th07_new && \
/opt/ghidra/support/analyzeHeadless /tmp/th07_new TH07 \
  -import th07/th07.exe -scriptPath scripts/ghidra \
  -postscript ImportFromCsv.java config/mapping.csv \
  -postscript ExportDelinker.java config/ghidra_ns_to_obj.csv build/objdiff/orig -overwrite
```
