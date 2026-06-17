# TH07-RE 反编译重建进度

最后更新：2026-06-17

## 终极目标

完整重现东方妖妖梦 `th07.exe`（1721 函数）的源码。**编译产物的游戏行为必须与原 exe 一致**。
objdiff match% 作为忠实度指标（目标每模块平均 ≥90%）。

## 当前总览

| 指标 | 值 |
|---|---|
| objdiff 跟踪模块 | 23 |
| objdiff 跟踪函数 | 228 |
| 模块平均 match% | 75.81% |
| ≥90% 模块 | 10（核心完成）|
| 80–90% 模块 | 4（接近达标）|
| 50–80% 模块 | 4（进行中）|
| <50% 模块 | 5（阻塞/早期）|
| normal build | 全部编译通过；链接需 183 跨模块符号实现 |

> match% 来源：`objdiff-cli diff` 的 `left.symbols[].match_percent`。模块平均为
> simple average（各函数 match% 的算术平均）。捕获脚本：`/tmp/objdiff_baseline.sh`。

## 每模块 match% 明细

`<<` = <50%（阻塞），`*` = <90%（待抛光）。函数按字节大小降序。

### 核心完成（≥90%）— 10 模块

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

### 接近达标（80–90%）— 4 模块

**ScreenEffect 89.89%** (13) — AddedCallback/DrawFadeOut/DrawFlickerFade 100×3, DrawFadeIn 98.06, Clear 97.54, SetViewport 97.00; DrawSquare 79.82, RegisterChain 81.95, ShakeScreen 82.50, CalcFlickerFade 78.61
**GameManager 89.36%** (6) — OnDraw 100, CutChain 98.42, DeletedCallback 94.65, OnUpdate 90.63; RegisterChain 76.73, **AddedCallback 75.76**（2726B 分配器）
**Player 89.00%** (9) — StartFireBulletTimer 100, RegisterChain 99.88, OnDrawLowPrio 99.50, Die 99.02, CutChain 98.42; AngleToPlayer 88.15, DeletedCallback 83.27; **ScoreGraze 71.69, CalcItemBoxCollision 61.08**（浮点栈布局）
**EffectManager 84.18%** (17) — Reset/EffectCallbackStill/EffectUpdateCallback4Init 100×3, CutChain 99.00, RegisterChain 98.09, EffectUpdateCallback4 99.28, SpawnParticlesWithVelocity 96.95; **OnDraw 65.40, EffectCallbackAttract 54.93, EffectCallbackAttractSlow 53.65, EffectManager(ctor) 35.40**

### 进行中（50–80%）— 4 模块

**AsciiManager 73.06%** (16) — AddFormatText 99.78, CutChain 99.00, AddedCallback 90.19; RegisterChain 58.78, InitializeMenuVms 61.11; **DrawStrings 49.66, AsciiManager(ctor) 39.51, DrawPopups 1.34**（大函数）
**SoundPlayer 71.34%** (14) — StopBGM 92.70, BackgroundMusicPlayerThread 88.85, GetWavFormatData 87.13; **ProcessSoundQueues 9.86, SoundPlayer(ctor) 11.56**（大函数）
**CMyFont 70.75%** (5) — Reset 99.81, Clean 95.00, InitWrapper 91.86; Init 64.88; **Print 2.20**（GDI 渲染大函数）
**Supervisor 69.18%** (14) — OnDraw 99.55, TickTimer 97.95, ReadMidiFile 91.49, PlayMidiFile 86.27; SetupDInput 77.16, RegisterChain 76.48, FadeOutMusic 73.64, StopAudio 70.36, AddedCallback 59.56, OnUpdate 56.06, DeletedCallback 56.87; **LoadConfig 30.60, DrawFpsCounter 27.01**

### 阻塞/早期（<50%）— 5 模块

**AnmManager 41.07%** (15) — CreateEmptyTexture 99.40, ReleaseTexture 88.28; LoadTexture 76.73; LoadAnm 59.97, ReleaseAnm 59.65, LoadSprite/SetActiveSprite 57; AnmManager(ctor) 44.66; **ExecuteScript 0.21（13178B）, DrawInner 2.21, LoadAnmEntry 2.32, SetRenderStateForVm 2.56, LoadTextureAlphaChannel 2.82, LoadTextureFromMemory 4.61**
**BombData 39.64%** (24) — MarisaABombDraw 87.99, MarisaBBombDraw 87.67, ReimuABombDraw 63.86 等 12 draw 完成；MarisaABombCalc2 42.75；**11 calc 未实现**（YoumuBBombCalc 1.23, SakuyaABombCalc2 1.41, ReimuABombCalc 1.29 等，每个 800–2400B）
**ReplayManager 34.70%** (12) — StopRecording 99.67, DeletedCallback 89.47; RegisterChain 84.78; **SaveReplay 4.36, RewriteReplay 2.73, AddedCallbackDemo 1.61, AddedCallback 2.09, OnUpdate 5.00 等**
**Pbg4Parser 19.72%** (3) — AdvanceNode 32.55, SetIndex 26.61; **Reset 0.00**（LZSS 字典/节点表初始化）
**ItemManager 0.00%** (0) — OnUpdate 4297B 单体巨型函数，未实现

## 剩余工作（按优先级）

### P0：让 normal build 链接成可启动 exe

normal build 全部 .cpp 编译通过，但链接失败：**183 个 unresolved 跨模块符号**。

构成：62 stdcall（`@Func@N`）+ 10 cdecl/data（`_Func`/`_g_Foo`）+ 111 C++ 方法（`?Method@Class@th07@@...`）。

**关键缺失函数**（已实现模块引用、但 owning 模块未逆向）：
- `Player::OnUpdate`、`Player::OnDrawHighPrio`（Player.cpp 缺这两个大函数）
- `GameManager::AddedCallback`（2726B 分配器，当前 75.76% 但函数体在 GameManager.cpp）
- `GameManagerScore::AddScore`/`IncreaseSubrank`/`AddGrazeScoreOnly`（Player.cpp 引用）
- `AnmManager::Draw*`/`ExecuteScript`（多个模块引用）
- `SoundPlayer::ProcessSoundQueues`、`MidiOutput::ProcessMsg`

**关键缺失数据全局**：
- `g_Pbg4Dict`/`g_Pbg4Nodes`/`g_Pbg4CurIndex`（LZSS 静态数据）
- `g_ScoreSubObj`、`g_CurrentStage`、`g_EffectMgrSpawnObj` 等

**路径**：(a) 逐模块逆向实现这些函数；(b) 扩展 `generate_stubs.py` 自动为
mapping.csv 中所有未实现符号生成 no-op stub（让 exe 先链接启动，再逐步替换为真实实现）。
注意 C++ mangled 成员方法（`?Method@Class@...`）不能简单 extern "C" stub，需在
owning 模块的 .cpp 里定义。

### P1：提升低匹配模块（<50%）

- **AnmManager 41%**：核心阻塞。`ExecuteScript`（13178B 操作码解释器）、`DrawInner`、
  `LoadAnmEntry`、`SetRenderStateForVm` 等大函数未实现。这是精灵/动画核心，不实现则
  无任何精灵渲染。
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

### P0.5：raw address 全量迁移到 typed C++（对齐 th06 标准）

项目宪法（AGENTS.md §2）已升级为纯 typed C++ 标准——禁止 raw 绝对地址。`src/Player.cpp`
已作为范本完成迁移（35 处 raw address → 零，match% 89.00→**89.04%**，证明 typed C++
不比 raw address 差）。剩余文件按密度从高到低迁移：

| 文件 | raw addr 数 | 状态 |
|---|---|---|
| `src/Player.cpp` | 35 → **0** | ✅ 完成（范本）|
| `src/Supervisor.cpp` | 161 | 待迁移（singleton member + rdata string + code addr）|
| `src/GameManager.cpp` | 146 | 待迁移（singleton member + data global）|
| `src/BombData.cpp` | 102 | 待迁移（data global float const + singleton member）|
| `src/ScreenEffect.cpp` | 46 | 待迁移（singleton member + rdata string）|
| `src/AsciiManager.cpp` | 43 | 待迁移（singleton member）|
| `src/EffectManager.cpp` | 28 | 待迁移（data global float const）|
| 其余 hpp/cpp | ~131 | 待迁移（零散）|

**迁移规则**（详见 AGENTS.md §2 + Player.cpp 范本）：
- singleton member（`0x575aa8`）→ `g_Supervisor.curState`（查 hpp 偏移注释）
- rdata string（`0x496fe0`）→ 字符串字面量 `"bgm/thbgm.fmt"`
- rdata float const（`0x498a54`）→ `extern "C" const f32 g_X = 1.0f;` + SYMBOL_MAP
- data global（`0x4d44f8`）→ `extern "C" i32 g_BombIsActive;` + SYMBOL_MAP
- code addr（`0x438668`）→ typed extern 声明 `Supervisor_Callback6()`
- ECX 单例参数（`0x4ba0d8`）→ `&g_SoundPlayer`

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
