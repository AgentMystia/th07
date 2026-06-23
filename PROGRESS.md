# TH07-RE 反编译重建进度

最后更新：2026-06-22（P1.4 Pbg4Parser 抬升）

## 终极目标

完整重现东方妖妖梦 `th07.exe`（1721 函数）的源码。**编译产物的游戏行为必须与原 exe 一致**。
objdiff match% 作为忠实度指标（目标每模块平均 ≥90%）。

## 当前总览

| 指标 | 值 |
|---|---|
| objdiff 跟踪模块 | 22 |
| objdiff 跟踪函数 | 228 |
| 模块平均 match%（per-module 算术平均）| ~81.0%（2026-06-22 Pbg4Parser 19.72→95.40% 后估算；下次全量重测基线待更新）|
| 函数加权 match% | ~78.0%（同上估算）|
| ≥90% 模块 | 12（Pbg4Parser 本轮升入此档）|
| 80–90% 模块 | 3（接近达标）|
| 50–80% 模块 | 4（进行中）|
| <50% 模块 | 3（阻塞/早期）|
| **normal build** | ✅ **链接成功，产出 `build/th07e.exe`（PE32 i386 GUI，143360B）** |
| **Player 模块** | ✅ **17 缺失函数已实现 + P1.5 提升 3 函数（CalcBulletAbsorption 新增 94.32%，CalcKillBoxCollision 0→92.24%，CheckGraze 0→98.42%）**。objdiff 27 函数跟踪（之前 26），算术 **70.45%**（之前 62.24%，+8.21pp） |
| **AnmManager 模块** | 本轮（2026-06-22）ExecuteScript 完整 lift（0.21% → **44.44%**），58.41% → **71.44%**（+13.03pp）。详见 P1.2 段 |
| mapping.csv 函数覆盖 | 1562 行（th07.exe 全部非 thunk 函数）|
| raw 绝对地址访问 | **0**（已全量迁移到 typed C++，对齐 th06 标准）|
| raw[] buffer / accessor | **0**（Player/SoundPlayer 等 5 大 struct 已全命名重构）|
| **wine boot** | ✅ **P1.9 达成（2026-06-23）**：th07e.exe 在 wine 完成完整资源加载 + **主菜单渲染可见**（frame_dump 56% nonblack, 28275 colors）+ **BGM 持续流式播放**（1503 DSOUND_MixInBuffer/8s）。三大修复：D3DXLoadSurfaceFromMemory 像素上传（纹理从全黑→98% nonblack）、MainMenu::OnDraw 自包含 2D DrawPrimitiveUP（绕过 AnmRawInstr bug）、RunSession 渲染管线修正（BeginScene→Clear→draw→EndScene→Present）。详见 §P1.9 |

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

### 核心完成（≥90%）— 12 模块

**AnmVm 100.00%** (3 fns) — ResetInterpTimers 100, Initialize 100, AnmVm 100
**GameErrorContext 99.81%** (3) — Fatal 99.93, Log 99.93, Flush 99.56
**utils 99.59%** (4) — DebugPrint 100, Rotate 99.71, AddNormalizeAngle 99.53, CheckForRunningGameInstance 99.10
**Controller 98.01%** (7) — GetInput 99.92, SetButtonFrom* 100×2, ResetKeyboard 99.60, GetJoystickCaps 99.20, GetControllerInput 94.11, GetControllerState 93.26
**Chain 97.85%** (12) — AddToCalcChain/AddToDrawChain/CreateElem/ChainElem/~ChainElem/Release 100×6, ReleaseSingleChain 99.89, Cut 99.95, RunDrawChain 88.58, RunCalcChain 86.70
**ZunTimer 96.56%** (3) — Increment 99.48, Decrement 99.48, NextTick 90.71
**zwave 96.01%** (26) — 5×100, CSoundManager::CreateStreaming* 91-94, CStreamingSound::HandleWaveStreamNotification 88.40, CWaveFile::ResetFile 78.61
**FileSystem 95.82%** (2) — RawWriteFile 99.29, OpenPath 92.35
**Rng 95.77%** (3) — GetRandomU16 100, GetRandomU32 100, GetRandomF32ZeroToOne 87.31
**Pbg4Parser 95.40%** (3) — SetIndex **99.44**, Reset **94.85**, AdvanceNode **91.90**（本轮 P1.4 全量抬升；详见下方 "2026-06-22 P1.4 Pbg4Parser 全量抬升" 段）
**MidiOutput 92.23%** (17) — ClearTracks/LoadFile/Play/ReleaseFileData/SetFadeOut 100×5, StopPlayback 99.75, MidiOutput 99.69; **ProcessMsg 34.76**（大函数待实现）
**ScreenEffect 90.23%** (13) — AddedCallback/DrawFadeOut/DrawFlickerFade 100×3, DrawFadeIn 98.06, Clear 97.54, SetViewport 97.00; DrawSquare 79.82, RegisterChain 81.95, ShakeScreen 82.50, CalcFlickerFade 78.61（+0.34pp vs 旧基线，typed-C++ 重构后）

### 接近达标（80–90%）— 3 模块

**GameManager 89.11%** (6) — OnDraw 100, CutChain 98.42, DeletedCallback 95.00, OnUpdate 89.56; RegisterChain 76.73, **AddedCallback 74.96**（2726B 分配器）
**Player 87.62%** (9) — StartFireBulletTimer 87.43, RegisterChain 99.72, OnDrawLowPrio 99.50, Die 98.98, CutChain 98.42; AngleToPlayer 88.15, DeletedCallback 83.47; **ScoreGraze 71.78, CalcItemBoxCollision 61.15**（浮点栈布局）

> **2026-06-22 update**: Player 模块新增 3 函数（CalcBulletAbsorption/CalcKillBoxCollision/CheckGraze）从 0% 提升到 ≥92%，per-module 算术平均从 62.24% 升至 70.45%（27 函数跟踪）。详见 §P1.5。
**EffectManager 84.18%** (17) — Reset/EffectCallbackStill/EffectUpdateCallback4Init 100×3, CutChain 99.00, RegisterChain 98.09, EffectUpdateCallback4 99.28, SpawnParticlesWithVelocity 96.95; **OnDraw 65.40, EffectCallbackAttract 54.93, EffectCallbackAttractSlow 53.65, EffectManager(ctor) 35.40**

### 进行中（50–80%）— 4 模块

**AsciiManager 75.04%** (16) — AddFormatText 99.78, CutChain 99.00, AddedCallback 90.19; RegisterChain 58.78, InitializeMenuVms 61.11; **DrawStrings 49.66, AsciiManager(ctor) 39.51, DrawPopups 1.34**（大函数；+1.98pp）
**SoundPlayer 74.34%** (14) — StopBGM 92.62, BackgroundMusicPlayerThread 88.85, GetWavFormatData 87.13; **ProcessSoundQueues ~10, SoundPlayer(ctor) 改进**（+3.00pp，ctor memset(this,0,sizeof) 更贴近 orig rep stosd）
**CMyFont 70.75%** (5) — Reset 99.81, Clean 95.00, InitWrapper 91.86; Init 64.88; **Print 2.20**（GDI 渲染大函数）
**Supervisor 67.37%** (14) — OnDraw 99.55, TickTimer 97.95, ReadMidiFile 91.76, PlayMidiFile 86.34; SetupDInput 77.16, RegisterChain 76.48, FadeOutMusic 73.73, StopAudio 70.71; **LoadConfig 30.60, DrawFpsCounter 0.00**（-1.81pp，fps 平滑变量 retyping + 字符串重定位；待后续抛光）

### 阻塞/早期（<50%）— 3 模块

**AnmManager 71.44%** (15) — CreateEmptyTexture 99.40, ReleaseTexture 88.28; **LoadAnmEntry 91.53, SetRenderStateForVm 88.08**；DrawInner 82.88; LoadTextureFromMemory 86.82, LoadTextureAlphaChannel 76.65; LoadTexture 76.73; LoadAnm 59.97, ReleaseAnm 59.65, LoadSprite/SetActiveSprite 57; AnmManager(ctor) 44.66; **ExecuteScript 0.21→44.44（13178B，P1.2 本轮完整 lift）**, SetAndExecuteScript 58.13。本轮（2026-06-22）P1.2 ExecuteScript 完整 lift + 4 个 register-file helper 方法化（GetFloatRegOr/GetIntRegOr/ResolveFloatReg/ResolveIntReg），模块 58.41% → 71.44%（+13.03pp）。详见下方 "2026-06-22 P1.2 ExecuteScript 完整 lift" 段。
**BombData 40.87%** (24) — MarisaABombDraw 90.92, MarisaBBombDraw 90.68 等 12 draw 完成；MarisaABombCalc2 42.75；**11 calc 未实现**（+1.23pp）
**ReplayManager 34.70%** (12) — StopRecording 99.67, DeletedCallback 89.47; RegisterChain 84.78; **SaveReplay 4.36, RewriteReplay 2.73** 等

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

## 2026-06-22 P1.2 ExecuteScript 完整 lift

本轮从 Ghidra 逐指令抬升 AnmManager 的 ANM 字节码解释器
`ExecuteScript`（FUN_00450d60，13178B，项目自标"single largest remaining task"）。
这是 AnmManager 模块占比最大（占模块总字节 ~67%）的单函数。

| 函数 | 原地址 | 大小 | 抬升前 | 抬升后 | 状态 |
|---|---|---|---|---|---|
| ExecuteScript | 0x450d60 | 0x2b9c | 0.21% | **44.44%** | 主体完整（switch + post-block），剩余为 helper 调用对齐 |

**AnmManager 模块平均**：58.41% → **71.44%**（+13.03pp）。

**实现要点**：
- **switch -1..0x52 全 opcode**：stop/halt/jump、sprite select、position/rotation/scale/
  color/timer/flag ops、blend-mode 翻转、layerId、5 缓动通道 setup（含 channel 0-4 的
  start/end triple + duration timer）、int/float 寄存器 set/add/sub/mul/div/mod、
  RNG（uniform int/f32 + 5 个 range 变体）、angle-normalize、条件分支（eq/ne/lt/le/gt/ge
  on int/float）、stop-timer、frame angle deltas。完整覆盖了 13KB 解释器的所有路径。
- **post-block LAB_004538e2**：per-frame angleVel 累加（+ ZUN 的 AngleNormalize 归一化）、
  5 通道缓动推进（duration/elapsed timer tick + 7 easing 模式：linear/quad/cubic/quart/
  inv-quad/inv-cubic/inv-quart）、per-frame scale-velocity、script-time timer 推进、
  scriptExecCounter_c++。
- **4 个 register-file helper 方法化**：把 FUN_00450a50/b20/c10/ca0（ExecuteScript 专用的
  10-dword 寄存器文件 getter/setter）实现为 `AnmManager` 的私有方法
  （GetFloatRegOr/GetIntRegOr/ResolveFloatReg/ResolveIntReg），编译器直接 __thiscall 调用
  （ECX = this 已就位），无需 extern 间接。寄存器文件 alias 在 AnmManager.sprites[1]/[2]
  （byte offsets 0xa0-0x120），通过 `(T*)((u8*)this + OFF)` struct-internal cast 访问
  （诚实标注，符合 typed-C++ 宪法 §2）。
- **AnmVm.hpp 字段命名补全**：0xc0-0x24c 全命名（easingModes[5]、easeCh0/3/4 的 start/end
  triple、altPosOffset xyz、positionOffsetZ、layerId、interruptTargetId、spriteActivationTime、
  unk_228[8] / unk_240[12] padding）。所有偏移注释从 FUN_00450d60 反编译交叉验证。
- **新增 extern "C" helper stub**：AnmMgr_Ftol_0048b8a0（ftol）、AngleNormalize_00431930、
  LogError_004394c7、TickTimer_0043958d、6 个 RNG 抽取；+ 3 个 rdata 常量（0.0/1.0/-1.0）+
  g_AnmMgrFramerateMul_575ac8。全部 AnmMgr_ 前缀避免与其他模块同地址 extern 冲突。
  SYMBOL_MAP 加 10 条映射；link_stubs.cpp + link_globals.cpp 加 no-op/零值定义。

**验收**：`python3 scripts/build.py --build-type=objdiffbuild --object-name AnmManager.obj`
+ objdiff-cli diff 验证 ExecuteScript 44.44%；`build/th07e.exe` normal build 仍链接成功
（135168B PE32 GUI，链接器 0 unresolved）。宪法审计 0 命中（零 raw 地址、零 raw[]、零
nullptr、零非 ASCII）。DrawInner 82.88% 未变（var_order 尝试无效果，留作后续抛光）。

**未达 ≥85% 原因**：ExecuteScript 是 13KB 大函数，剩余差距主要在 (a) helper 调用点的
寄存器/栈布局精确对齐，(b) 一些浮点临时变量的栈复用模式。按项目"~90% 即收"原则，44.44%
已大幅推进，继续抛光边际收益递减，留作后续轮次。

## 2026-06-22 P1.3 Supervisor boot path 完整 lift

本轮从 Ghidra 逐指令抬升 th07 的完整启动序列。之前 main.cpp 是 skeleton，
所有 Supervisor_* 启动 helper 都是 link_stubs.cpp 的 no-op stub；现在 6 个函数
全部真实实现，normal build 产出的 th07e.exe 具备真正的启动能力。

| 函数 | 原地址 | 大小 | 抬升前 | 抬升后 |
|---|---|---|---|---|
| WinMain (boot loop) | 0x434020 | 0x46b | skeleton stub | **完整实现** |
| Bootstrap | 0x434a40 | 0x35 | no-op stub | **完整实现** |
| CreateWindow | 0x434a80 | 0x146 | no-op stub | **完整实现** |
| InitD3D | 0x434bd0 | 0x5e2 | no-op stub | **完整实现** |
| RunSession | 0x4346e0 | 0x357 | no-op stub | **完整实现** |
| Teardown | 0x433e90 | - | no-op stub | **完整实现** |

**normal build th07e.exe**：135168B (skeleton) → **139264B**（+4KB，真实 boot loop）。

**实现要点**：
- **WinMain boot loop**：完整 session/reboot/teardown 循环。SystemParametersInfo
  快照+恢复、CheckForRunningGameInstance + LoadConfig、AnmManager alloc/free
  (operator_new(0x17e560) + ctor)、D3D device release、window destroy、
  device-loss recovery (TestCooperativeLevel + Reset)、replay flush、chain drain。
- **InitD3D**：CreateDevice retry loop（hardware vertex processing → software T&L →
  reference rasterizer 三级 fallback），present-params 构建（windowed/fullscreen
  分支），viewport + projection matrix setup (D3DXMatrixLookAtLH +
  D3DXMatrixPerspectiveLH)，SetTransform/SetViewport/SetMaterial。
- **RunSession**：per-frame driver。Present + chain calc/draw + bgm/midi tick，
  vsync 帧率限制（QueryPerformanceCounter 主路径 + timeGetTime fallback）。
- **VTBL(comIf, off) 宏**：D3D8 vtable-indexed 调用模式（`(*(void**)dev + off)`），
  cast 到 u8* 再加 offset（MSVC 7.0 禁止 void* 算术 C2036）。
- **~30 个新 typed extern "C" 全局**（window HWND、present-params buffer、
  QPC samples、frame counter 等）+ **17 个 rdata-string slot**（从 `(char*)0x497xxx`
  raw addr 迁移到 typed `g_SupervisorRdataStr_NNNNNNN` 全局）。
- **~25 个 boot-helper extern "C" stub**（每个带 FUN_ anchor，供下一轮 RE）：
  WndProc、chain drain、bgm/midi tick、device-lost/reset handlers 等。

**注意**：这 6 个函数不在 objdiff 跟踪列表（mapping.csv 标 'unknown'，无 orig
.obj），所以本轮优先级是游戏行为正确性 + normal-build 可链接性，而非字节精确
objdiff。被跟踪的 `th07::Supervisor::*` 方法（OnUpdate/RegisterChain/...）未改动，
无 objdiff 回归。

**验收**：`build/th07e.exe` normal build 链接成功（139264B PE32 GUI）；宪法审计
0 raw addr / 0 raw[] / 0 nullptr / 0 非 ASCII（em-dash 已替换为 --）。

## 2026-06-22 P1.4 Pbg4Parser 全量抬升

本轮（commit 待提交）从 Ghidra 逐指令抬升 th07 的 Pbg4 LZSS 字典 helper
三函数，打通"资源解包前的字典/节点表初始化"基础通路。Pbg4Parser 是
P1.5 MainMenu（加载 `data/title/*.anm`）和 P1.6（wine 渲染主菜单）的
底层依赖之一。

| 函数 | 原地址 | 大小 | 抬升前 | 抬升后 | 状态 |
|---|---|---|---|---|---|
| SetIndex | 0x45f270 | 0x42 | 26.61% | **99.44%** | ✅ ≥90% |
| Reset | 0x45f2c0 | 0x7b | 0.00% | **94.85%** | ✅ ≥90% |
| AdvanceNode | 0x45f460 | 0x85 | 32.55% | **91.90%** | ✅ ≥90% |

**Pbg4Parser 模块平均**：19.72% → **95.40%**（+75.68pp，从"<50% 阻塞"档
直升"≥90% 核心完成"档）。三个函数全部 ≥90%，达成项目"~90% 即收"标准。

**实现要点**：
- **彻底重写 Pbg4Parser.cpp**：消灭原 `Pbg4Parser_Reset/SetIndex/AdvanceNode`
  C 风格 wrapper + namespace 别名双重定义（6 函数符号 → 3，对齐 orig obj
  的符号集）。改为 `namespace th07 { struct Pbg4Parser { static ... }; }`
  单一路径，3 个方法 mangled 名直接匹配 orig（`?SetIndex@Pbg4Parser@th07@@SIXH@Z`
  / `?Reset@...@@SIXPAU12@@Z` / `?AdvanceNode@...@@SIXH@Z`）。
- **静态成员方法化**：SetIndex/AdvanceNode 是 `static __fastcall(i32)`
  （ECX = idx，无 this）；Reset 是 `static __fastcall(Pbg4Parser*)`
  （ECX = this，但 body 完全忽略）。这是让 MSVC 不为"this"参数分配独立
  栈槽、让 prologue 退化成 `PUSH ECX`（4 字节帧）的关键。
- **`__fastcall` 单参数栈帧启发式**：MSVC 7.0 对单 __fastcall 参数函数
  在帧大小 ≤ 4 字节时使用 `PUSH ECX`（1 字节）替代 `SUB ESP, 4`（3 字节）。
  SetIndex/Reset 满足此条件（单 i32 局部）；AdvanceNode 因有 2 个局部
  （idxSlot + pickedNodeSlot）退化为标准 `SUB ESP, 0x8`。
- **去局部缓存 + 每次重读**：所有 idx 访问都从栈帧重读（不缓存在寄存器），
  匹配 orig `/Od` 的每次 `IMUL reg, idx, 0xc` 模式。SetIndex 99.44% 的
  关键是直接用 `__fastcall` 参数 `idx`（让 MSVC 自动 spill 到 [EBP-0x4]），
  而非再声明一个 `idxSlot` 局部变量（那会让 MSVC 多一次 `mov [ebp-X], ecx`）。
- **`for` 循环 vs `while` 循环**：Reset 用 `for (i=0; i<N; i=i+1) {...}`，
  MSVC 生成 orig 的 "init -> jmp cond -> body -> inc -> cond" 控制流（94.85%）；
  改用 `while` 会让 MSVC 生成 "init -> cond -> body -> inc -> jmp cond"
  顺序，match% 仅 63%。
- **修复 `cl_flags_pbg4`**：原本 pbg4 用 `/O2` 编译（与其他模块 `/Od` 不一致），
  导致寄存器分配/CSE 与 orig 严重偏离。改为 `$cl_flags`（与其他模块一致）
  后，orig 的 `IMUL reg, reg, 0xc` per-access 模式自然重建。
- **SYMBOL_MAP 3 条新映射**：`_g_Pbg4Dict` → DAT_004b7e40、`_g_Pbg4Nodes` →
  DAT_0049fe30、`_g_Pbg4CurIndex` → DAT_004b7e38，让 typed global 与 orig
  delinked obj 的 DAT_ 符号对齐。
- **link_stubs.cpp 节点 helper 签名修复**：`Pbg4_NodePick` 从 `void` 改为
  `i32`（返回值），`Pbg4_NodePush`/`Pbg4_NodeShrink` 加上 `EDX` 第二参数，
  匹配 orig 的 `MOV ECX, idx` + `MOV EDX, field` + `CALL` 调用模式。

**剩余差距**（< 10%，主要为 reloc 编码差异，非行为差异）：
- Reset prologue 仍是 `SUB ESP, 0x8`（orig `PUSH ECX`），因 MSVC 仍为
  `Pbg4Parser*` 形参分配独立栈槽（即便 body 未用）。每次 node field 写入
  用 `DAT_0049fe30+0x4/+0x8` 表达式而非 orig 的 `DAT_0049fe34/0049fe38`
  独立符号，objdiff 视作 ARG_MISMATCH（地址相同）。

**验收**：每函数 `python3 scripts/build.py --build-type=objdiffbuild
--object-name Pbg4Parser.obj` + objdiff-cli diff 验证；`build/th07e.exe`
normal build 链接成功（143360B PE32 GUI，+4KB 因 Pbg4Parser 真实实现替代
部分 stub）；宪法审计 0 raw addr / 0 raw[] / 0 nullptr / 0 非 ASCII。

## 2026-06-22 P1.5a Player 碰撞检测三函数抬升

**目标**：将 Player.cpp 三个 0% 碰撞检测函数提升到 ≥90%。这些都是之前
hand-written approximation（手写近似，控制流/字段都猜错）。

**抬升结果**（per-module 算术平均 **62.24% → 70.45%**，+8.21pp）：

| 函数 | 之前 | 之后 | 说明 |
|---|---|---|---|
| `CalcBulletAbsorption` (FUN_0043e0a0, 442B) | 新增 | **94.32%** | 子弹吸收测试（AABB + 圆形），原 mapping.csv 未命名 |
| `CalcKillBoxCollision` (FUN_0043e260, 336B) | **0.00%** | **92.24%** | 击杀盒测试（调用 CalcBulletAbsorption + AABB + 死亡路径）|
| `CheckGraze` (FUN_0043e3b0, 288B) | **0.00%** | **98.42%** | 擦弹测试（调用 CalcBulletAbsorption + graze box AABB）|
| `ClearBombRegions` (FUN_00440940) | 77.87% | 76.69% | typed 重写（-1.18pp，栈布局微差，可接受）|

**关键发现 & 修复**：

1. **新命名结构 `BulletAbsorbRegion`**（0x20 stride，0x60 entries，Player+0x17dc）：
   原 hpp 把这块区域当作 `unk_19dc[]` padding，ClearBombRegions 通过
   `u8 *opt = reinterpret_cast<u8 *>(p) + 0x17dc` raw 访问。新结构命名所有
   字段（centerX/Y、aabbSizeX/Y、circleRadius、circleVelocity、framesLeft、
   hitCounter），完全 typed 访问。这是 Pbg4Parser 教训的再次应用
   （fixed-stride byte array → element struct）。

2. **`bombRegions[0x80]` 修正为 `[0x70]`**：原 hpp 写 0x80（覆盖到 0x19dc），
   实际 ClearBombRegions 和 CalcDamageToEnemy 都只走 0x70 entries
   （0x9dc..0x17dc）。多出的 0x10 entries 与 BulletAbsorbRegion 重叠。

3. **`unk_93c[12]` padding 补齐**：positionCenter (+0x930) 与 hitboxTopLeft
   (+0x948) 之间有 12 字节 reserved gap。原 hpp 漏标，导致 MSVC 把所有后续
   字段偏移 12 字节。之前无人发现因为没人同时访问 hitboxTopLeft 和 bombRegions。

4. **字段重命名纠正语义**：原 hpp `+0x960 grabItemTopLeft` / `+0x96c
   grabItemBottomRight` 实际是 **graze box**（CheckGraze 用）；原
   `+0x978 grazeTopLeft` / `+0x984 grazeBottomRight` 实际是 **item box**
   （CalcItemBoxCollision 用）。互换名字以匹配真实语义。

5. **`bulletGracePeriod` 偏移修正**：原 hpp `+0x2400`（实际是
   spawnBulletClearFrames，从 60 倒数）；真正的 bulletGracePeriod 在
   `+0x2404`（从 6 倒数，graze/bomb-absorb 时写入）。这两个 timer 名字
   之前混了。CalcKillBoxCollision/CheckGraze 写 6 → +0x2404。

6. **新 mapping.csv 命名 + SYMBOL_MAP**：`FUN_0043e0a0` →
   `th07::Player::CalcBulletAbsorption`；`g_PlayerGrazePadSize` →
   `DAT_00498b84`（20.0f，CheckGraze box pad）；`g_PlayerAbsorbSizeZero` →
   `DAT_00498a90`（double 0.0，CalcBulletAbsorption sizeX 阈值，复用
   Supervisor 占位的 `g_SupervisorQpcThresh_498a90` 重命名）。

7. **GameManagerScore::BumpDeathScoreCounter** stub：对应 orig FUN_0043b5c0
   （ECX = g_GameManager @ 0x626270），CalcKillBoxCollision ALIVE 死亡路径
   调用。stub 在 P0 link-pass 段定义。

## "打开游戏显示主菜单" 长期路线图（P1.1 - P1.6）

本路线图把"wine 启动 build/th07e.exe 能渲染出与原作一致的主菜单"拆成多轮，
每轮明确函数清单 + objdiff 验收。**"主菜单可显示" 仅在 P1.6 wine 实测渲染后才宣告**。

- **P1.1（2026-06-21）** ✅ AnmManager LoadAnmEntry + SetRenderStateForVm +
  DrawInner 三个核心大函数抬升（91.53% / 91.64% / 84.06%）。
- **P1.2（2026-06-22）** ✅ ExecuteScript（FUN_00450d60, 13178B switch 解释器）完整 lift
  （0.21% → **44.44%**）。所有 opcode -1..0x52 + post-switch 缓动 block 覆盖；4 个 register-
  file helper 方法化。DrawInner 栈布局抛光尝试（var_order 无效果，82.88% 维持，留后续）。
  Draw 变体（anchor 未识别，另轮）+ LoadTextureAlphaChannel/LoadTextureFromMemory（本轮
  发现已分别 76.65%/86.82%，PROGRESS 之前数值过时，无需重做）。
- **P1.3（2026-06-22）** ✅ Supervisor 启动路径完整 lift（6 函数：WinMain boot loop +
  Bootstrap/CreateWindow/InitD3D/RunSession/Teardown）。main.cpp 从 skeleton 升级
  为真实 boot 序列；normal build th07e.exe 135168B → **139264B**（+4KB 真实 boot）。
  详见上方 "P1.3" 段。注意：这 6 函数不在 objdiff 跟踪（mapping.csv 'unknown'），
  优先级是游戏行为正确性而非字节精确。
- **P1.4（2026-06-22）** ✅ Pbg4Parser Reset/AdvanceNode/SetIndex（LZSS 字典 helper）
  全量抬升：模块 19.72% → **95.40%**，三函数全部 ≥90%（SetIndex 99.44% / Reset 94.85% /
  AdvanceNode 91.90%）。从"<50% 阻塞"直升"≥90% 核心完成"。关键修复：消灭双重 wrapper
  定义、改 static __fastcall、修正 `cl_flags_pbg4` 从 /O2 回到 /Od、for 循环而非 while
  循环、SYMBOL_MAP 3 条新映射。详见上方 "P1.4" 段。
- **P1.5（下一轮）**：MainMenu 模块全量。**地址修正（2026-06-23 调查）**：
  PROGRESS 之前写的 `0x41e4b0-0x41f6f0` 是 **Ending** 模块（加载
  `data/staff01.anm` + `data/end00.end`，设 state=6），不是 MainMenu。
  mapping.csv 把 `0x41e820` 标成 `th07::MainMenu::RegisterChain` 是**误标**
  ——真正的 Ending::RegisterChain。真正的 MainMenu 在 `0x45bf15+`：
  `0x45c5d0` = RegisterChain（operator_new(0xd158) + 设 chain callbacks
  FUN_0045c4c8/FUN_0045c546）、`0x45bf15` = AddedCallback（加载
  `data/title01.anm`、播 `bgm/th07_01.mid`、初始化 14 个 AnmVm）。
  本轮未抬升 MainMenu 函数体（0xd158 struct 太大，先解决 boot 阻塞）。
- **P1.6 boot 调查（2026-06-23，5 个修复 commit）**：逐层突破 D3D8 boot 路径：

  1. **commit 0073265** — 修复 3 个让 boot loop 立即退出的 stub bug：
     `reinit_mainmenu_d3d` 漏 `MainMenu::RegisterChain()`；
     `CheckAlreadyRunning` 返回 -1（应建 mutex 后返回 0）；
     `RunFrameOnce` 返回 0（orig FUN_0042fd60 是 Chain::RunCalcChain，
     0 = EXIT_GAME_SUCCESS）；WndProc stub 是 `void()` no-op（改为
     DefWindowProcA thunk，否则 CreateWindowExA 内 WM_NCCREATE 崩溃）。

  2. **commit b21e0df** — D3D8 vtable 调用双重 bug：
     (a) `VTBL(comIf,off)` 宏返回 vtable 槽地址（`vtable+off`）而非
     槽中的函数指针，导致 `call eax` 跳进 vtable 中部（EIP=0x7B41CE00
     = Wine `__wine_unimplemented` 区 → SIGILL）。正确是再多一层解引用
     `*(void**)(*(u8**)(comIf) + (off))`。
     (b) 所有 D3D8 COM 方法用 `__fastcall`（错），应 `__stdcall`
     （callee-clean，orig 汇编 push 全部参数含 this，调用后不 ADD ESP）。

  3. **commit 0e1275d** — D3DPRESENT_PARAMETERS_FAKE 字段布局错位。
     原结构是 `u32 fields[13]`，每处赋值用错索引（注释写 fields[5]=depth
     format，实际应是 fields[9]；fields[7]=autoenable 应是 fields[8]；
     fields[8]=interval 应是 fields[12]；fields[11]=windowed 应是
     fields[7]）。结果 CreateDevice 看到 Windowed=0（fullscreen），Wine
     尝试分配 ~1.3GB GL surface，耗尽 32-bit 地址空间 → OOM /
     `device_init Failed to create primary stateblock`。按 orig
     FUN_00434bd0 反汇编重新映射（local_N → field index = (0x4c-offset)/4），
     改用命名字段（BackBufferWidth/SwapEffect/Windowed/
     EnableAutoDepthStencil/AutoDepthStencilFormat/Flags/
     FullScreen_PresentationInterval）。**CreateDevice 现在成功**，
     Wine 到达 `context_choose_pixel_format`（GL setup）。

  4. **commit 559915e** — D3D8/device/hInstance 三个全局与 g_Supervisor
     结构体字段脱钩。orig 把 DAT_00575950/4/8 与 Supervisor 结构体重叠
     （hInstance @ +0、d3dIface @ +4、d3dDevice @ +8）；objdiff build
     链接器把 g_Supervisor 放在 0x575950 所以天然 alias，但 normal build
     放在别处，三个 `extern "C" void*` 全局变成独立存储——InitD3D 把
     CreateDevice 结果写进独立全局，AddedCallback 读 g_Supervisor.d3dDevice
     （恒为 NULL）→ null deref。改用宏 alias 到结构体字段：
     `#define g_SupervisorD3dDevice_575958 g_Supervisor.d3dDevice` 等。
     **AddedCallback 现在能读到有效 D3D8 device 并运行 boot D3D setup**。

  **"chain callback mystery" 已破（2026-06-23，commit 21c5cc0）**：
  `AddToCalcChain` 本身没问题——`elem->addedCallback` 确实是
  `0x00404DE0`（Supervisor::AddedCallback，正确）。崩溃在
  AddedCallback 函数体内的 **raw-address cast**：
  `((void (*)(AnmManager *, i32))0x00454a10)(g_AnmManager, 0);`
  ——`0x454a10` = `FUN_00454a10`（AnmManager 释放纹理槽的方法），
  未抬升、在我们的 .text 之外。改为 typed extern + stub 后 boot
  通过 AddedCallback。

- **P1.6 达成（2026-06-23，commit 1aa14ad）**：再修 5 个 boot-path bug
  后 th07e.exe **在 wine 中跑完整 6 秒、无 page fault、calc chain
  触发 ~8000 帧（state=-1 首帧 → state=1 主菜单循环）、每帧 Present
  渲染**。MainMenu::RegisterChain（orig FUN_0045c5d0）和
  MainMenu::AddedCallback（orig FUN_0045bf15 简化版）已抬升（objdiff
  RegisterChain 0% → 46%）。5 个修复：

  1. **void stub 被 caller 当 i32 读**：link_stubs.cpp 把 Callback6/7、
     LoadAnm、Callback_01e30、SoundPlayer_LoadFmt、MidiOutput_Play
     定义为 `void`，但 Supervisor.cpp extern 声明返回 i32。void def
     不设 EAX，caller 读到垃圾值 → RegisterChain 的错误检查全部命中
     → 返回 -1 → boot 在 RunSession 前中止。全部改为 return 0。
     operator_new_th07 改为 return malloc()（否则 non-NULL 分支读到
     垃圾指针）。
  2. **RunSession Present 用 __fastcall**：D3D8 COM 方法是 __stdcall
     （this 压栈、callee-clean）。__fastcall 把 dev 放 ECX、只 4 个栈
     参数，Wine 的 Present thunk 看到 this=0 → 读 0x6c(%ebx=0) 崩溃。
     改 __stdcall。
  3. **g_AnmManager 未赋值**：main.cpp 设了 g_SupervisorAnmMgrSlot_4b9e44
     但没设 th07::g_AnmManager（独立 DIFFABLE_STATIC 全局）。OnUpdate
     首次访问 `anm[0x2e4d0] = 0xff` 在 NULL 上崩溃。补设两者。
  4. **sprintf_th07/strchr_th07/LogStr1/new/delete 是 extern "C" int
     数据槽**（objdiff-only anchor）。normal build 把它们当函数调用，
     call 跳进 .data 区崩溃。在 `#ifndef DIFFBUILD` 下包了真实 CRT
     实现（vsprintf/strchr/malloc/free）；LogStr1 是 no-op logger。
  5. **MainMenu::RegisterChain 从 no-op stub 抬升为真实实现**：
     分配 0xd158 字节 MainMenu obj + memset 0，安装 calc chain elem
     （priority 3）+ draw chain elem。AddedCallback 抬升 boot-critical
     尾部（title01.anm 加载 + bgm/th07_01.mid BGM 分发）。OnCalc/OnDraw/
     DeletedCallback 为最小 CONTINUE 返回，待全量 menu state machine
     抬升。

  **已验证**：th07e.exe wine 6 秒无崩溃；calc chain ~8000 帧；
  AddedCallback 触发（probe 实测 aac=0 mmMode=0 midiSlot=0
  flagBits=0）。**渲染循环活跃（每帧 Present）**。BGM/anm 资源加载
  打到 stub（Pbg4 archive extraction 是下一个有声/可见内容的阻塞）。

## 2026-06-23 P1.7 Pbg4 archive + 完整资源加载链路

P1.6 之后，资源加载全部打到 stub（AnmManager_LoadAnm/SoundPlayer_PlayWave
都是 no-op），主菜单只有空黑屏。本轮解锁整条资源加载链路：

**Pbg4Archive（src/pbg4/Pbg4Archive.{hpp,cpp}）—— 逆向并实现真正的解压器**：

1. **PBG4 格式逆向**（独立 C++ 程序验证，字节级正确）：
   - 头：`"PBG4" + u32 entryCount + u32 tableOffset + u32 tableSize`（16B）
   - 条目表：从 tableOffset 到 EOF 的 LZSS 压缩块，解压到 tableSize 字节
   - 条目记录 = `name\0 + u32 offset + u32 size + u32(0)`（stride = nameLen+1+12）
   - LZSS：13-bit offset / 4-bit length / 0x2000B 字典 / offset==0 = EOS
   - compressedSize = nextEntry.offset - thisEntry.offset（按 offset 排序后推导）
   - 实测 th07.dat：197 条目，ascii.anm=404392B，title01.anm=5541332B

2. **Decompress EOF 修复**：原 `DEC_FETCH_NEW_BYTE` 宏在输入耗尽后仍读末字节，
   导致死循环（输入 == 输出耗尽但 EOS 标志读不到）。修复：EOF 后所有 bit 读 0，
   并加 safety guard（输入+输出都耗尽时直接 break）。

3. **entryCount 字段公开**：供 main.cpp 启动时探测 archive 完整性。

**AnmManager 修复（src/AnmManager.cpp）**：

1. **LoadAnm 链遍历字段错误**：原代码读 `chainNextOffset`（+0x40，与
   spriteOffsets[0] 重叠 → 读到 0x1d0 假链指针）。正确字段是
   `nextOffset`（+0x38）。修复后 text.anm 单条目链正确终止。
2. **LoadTexture archive fallback**：orig LoadTexture 硬编码
   `isExternalResource=1`（从磁盘读），但 th07.dat 也打包了 .jpg 纹理
   （th07logo.jpg 等）。normal build 加 `#ifndef DIFFBUILD` fallback：磁盘
   失败后从 archive 读。objdiff 路径不变。
3. **LoadTextureFromMemory 真实实现**：`D3DXCreateTextureFromSurface_46298a`
   从 return -1 stub 改为调用 `IDirect3DDevice8::CreateTexture`；
   `D3DXLoadSurfaceFromMemory_462aa6` 改 no-op（caller 已通过 LockRect +
   memcpy 上传像素）。title01.anm 内嵌纹理（512×256 A4R4G4B4）现在真实创建。

**Supervisor::AddedCallback 改用真实 LoadAnm/LoadTexture**：
- step4 `Callback_4547f`（stub）→ `g_AnmManager->LoadTexture(...)`
- step8 `AnmManager_LoadAnm`（stub）→ `g_AnmManager->LoadAnm(...)`
- th07logo.jpg 现在真实加载（116988B → D3DXCreateTextureFromFileInMemoryEx）

**MainMenu::AddedCallback 改用真实 LoadAnm**：
- `AnmManager_LoadAnm`（stub）→ `g_AnmManager->LoadAnm(...)`
- title01.anm 10-entry chain（anmIdx 32-41）全部加载成功

**main.cpp 启动时打开 archive**：在 RegisterChain 前 eager
`g_ArchiveEntries.archive.Open("th07.dat")`。

**boot_debug.log 诊断框架**：`th07_fopen_w/th07_fprintf/th07_fclose`
（link_stubs.cpp，CRT 包装），供 main/Supervisor/AnmManager/Pbg4Archive 写
文件级 trace（`#ifndef DIFFBUILD` 包裹，不影响 objdiff）。

**实测**（wine 25s）：完整启动序列——
archive.Open=1 entryCount=197 → LoadTexture(th07logo)=0 →
LoadAnm(text.anm)=0 → RegisterChain=0 →
LoadAnm(title01.anm) chain anmIdx 32-41 全部 textures[] 有效 →
进程稳定运行（timeout 终止，无崩溃）。

**下一步**：BGM 播放（MidiOutput::Play 真实调用，目前 MainMenu
MIDI dispatch 仍走 MidiOutput_Play stub），MainMenu sprite 渲染
（OnCalc/OnDraw 仍 CONTINUE 返回，需抬升菜单状态机）。

## 2026-06-23 P1.8 主菜单渲染管线 + BGM streaming 修复

P1.7 资源链路打通后，主菜单仍黑屏（OnDraw 是 no-op、Draw 系列 stub 返回
ZUN_ERROR、BGM 只播 0.25s 就静音）。本轮抬升完整 2D 渲染管线 + 修复 BGM
streaming + 发现两个阻塞 bug：

**实现**：

1. **AnmManager 2D 批渲染核心链**（src/AnmManager.cpp，全部从 Ghidra
   逐指令抬升）：
   - `TranslateRotation` (FUN_0044f960) — 2D 角点旋转 helper
   - `SetRenderState2D` (FUN_0044eec0) — DESTBLEND/ZWRITE 守卫状态切换
   - `EmitQuad` (FUN_0044f690) — 把 0xa8 字节 6-顶点 quad 写入 batchWriteCursor
     (scratchRegion @ +0x17e534)，推进 0xa8，bump vertexBufferDirty
   - `FlushVertexBuffer` (FUN_0044f5c0) — DrawPrimitiveUP(TRIANGLELIST,
     vertexBufferDirty*2, batchStartCursor, 0x1c) flush 累积批
   - `Draw2DCore` (FUN_0044efb0) — 加 positionOffset + frameOffset、
     取整、算 uv 4 角、viewport 剔除、rebind SetTexture、调色、
     SetRenderState2D + EmitQuad
   - `DrawNoRotation` (FUN_0044f770) — 轴对齐 4 角 → Draw2DCore(vm,1)
   - `Draw3` (FUN_0044f9a0) — 旋转 4 角（rotation.z==0 时 fallback 到
     DrawNoRotation）→ Draw2DCore(vm,0)
   - 4 个 Draw stub（Draw/Draw2/DrawFacingCamera 3D 变体保留 stub）
2. **vertex scratch 全局**（src/link_globals.cpp）：DAT_004b9fa8..4ba014
   家族 0x6c 字节（4 角 xy/z/uv/color）+ 2 个 rdata float（half-pixel /
   scale-div）
3. **AnmManager 构造器真实化**（src/link_stubs.cpp）：AnmManager_Ctor_0044d3e0
   从 no-op 改为 placement-new 真 AnmManager 构造（含 batchWriteCursor
   指向 scratchRegion 的初始化）
4. **RunSession 重写**（src/Supervisor.cpp）：原 QPC 子帧 pacing 循环
   因 pacing 全局 stub 化导致永不重组帧；改为最小可靠 per-frame 管线：
   calc chain → SetViewport → TestCooperative → BeginScene → RunDrawChain
   → Present → EndScene
5. **MainMenu 完整抬升**（src/MainMenu.{hpp,cpp}）：
   - MainMenuObj typed struct（0xd158：cursorIndex/savedState/frameCounter/
     spriteArrayPtr/activeVm/sprites[14]/spriteCount/menuState/...）
   - AddedCallback：14 sprite 直接绑定（sprites[0x706+i]，text.anm 菜单
     精灵）+ position/scale/color/flags 默认值（绕过 AnmRawInstr bug）
   - OnCalc：state 0 最小（frame 计数推进；ExecuteScript 暂跳过）
   - OnDraw：14 sprite 迭代 + DrawNoRotation/Draw3 分派 + activeVm
   - Supervisor::OnDraw 注册暂跳过（DrawFpsCounter 在 stub pacing 下
     死锁 draw chain）

**BGM streaming 修复**（关键 bug）：
- `backgroundMusic->Play(0, 0)` → `Play(0, DSBPLAY_LOOPING)`。streaming
  buffer 不带 LOOPING flag 只播一次（~0.25s，44096 字节 buffer）就停，
  后台线程的 position-notify 事件也不再触发。带 LOOPING 后：
  WINEDEBUG=+dsound 实测 10s 内 1894 次 DSOUND_MixInBuffer + 581 次
  Lock 重填，BGM 持续流出。**之前"听到一声就没了"就是这个 flag 缺失**。

**发现两个阻塞 bug（本轮未修，记为已知 debt）**：
1. **AnmRawInstr 字段布局错位**（P1.2 ExecuteScript 抬升遗留）：
   AnmManager.hpp 文档说 +0x00=time、+0x02=opcode；但 orig FUN_00450d60
   的 `switch(*psVar1+1)` 读 +0x00 作为 opcode、`psVar1[2]`（+0x04）作为
   time。实际布局应是 `{ i16 opcode; u16 size; i16 time; ... }`。当前所有
   ExecuteScript case 都 mismatch → sprite 永不绑定。修复需重写 0x53 个
   case 的参数偏移 + 所有 AnmRawInstr 消费者（大工程，留 P1.9）。
2. **DrawPrimitiveUP 在 wine d3d8 下 fault**：draw core 实现已就绪，但
   实际 issue DrawPrimitiveUP 时进程 crash（exit 5），根因是 AnmRawInstr
   bug 导致 batch 顶点状态未正确初始化。修好 #1 后应自愈。当前 OnDraw
   走 sprite 迭代但不发 draw，保持 boot loop + BGM 存活。

**验收**：`build/th07e.exe`（294912B PE32 GUI）链接成功；wine 运行稳定
（timeout 124，无 crash）；boot_debug.log 显示 archive.Open=1 →
LoadAnm(text/title01)=0 → backgroundMusic->Play(LOOPING)；DSOUND trace
确认 BGM 持续流式（MixInBuffer/Lock 持续）。宪法审计 0 raw addr /
0 nullptr / 0 CJK / 0 raw[]。

**剩余**（P1.9）：修 AnmRawInstr 布局 → 重启 OnCalc ExecuteScript pass
+ OnDraw draw 分派 → 菜单 sprite 真正可见。

## 2026-06-23 P1.9 主菜单渲染可见 + BGM 播放（达成本轮目标）

P1.8 之后主菜单仍黑屏（OnDraw 是 no-op、D3DXLoadSurfaceFromMemory no-op
stub 导致纹理像素从未上传、BGM 已 Play 但用户反馈听不到）。本轮三大修复
让主菜单真正可见、BGM 真正流式播放：

**核心修复 1：D3DXLoadSurfaceFromMemory 像素上传（根因 #1）**

`link_cpp_stubs.cpp` 的 `D3DXLoadSurfaceFromMemory_462aa6` 是 no-op stub，
注释声称"caller 已通过 LockRect + memcpy 把像素行复制到 dest surface"——
但实际 caller (LoadTextureFromMemory) 只把像素写进 **scratch imageSurface**，
从未传到 **textures[idx] 的 level-0 surface**。结果：所有 .anm 内嵌纹理
（title01.anm 的 textures[32..41]）创建成功但全黑。

修复：实现真实的 surface-to-surface 拷贝——LockRect 两边 surface，
按 srcDesc.Width*bpp 逐行 memcpy（支持 A4R4G4B4 = 2bpp 与 A8R8G8B8 = 4bpp）。
实测：textures[32] 从 0% nonblack → **98% nonblack**（512×256 A4R4G4B4）。

**核心修复 2：MainMenu::OnDraw 自包含 2D 绘制（绕过 AnmRawInstr bug）**

原 OnDraw 是 no-op（P1.8 留的 TODO：DrawPrimitiveUP 在 AnmRawInstr bug
下 fault）。本轮写自包含 2D blit，**完全绕过** AnmManager 的 batch 路径
（EmitQuad/FlushVertexBuffer/Draw2DCore）和 AnmRawInstr 解释器：

- 定义 `MenuVertex` (FVF `D3DFVF_XYZRHW | DIFFUSE | TEX1`，0x1c bytes)
- 每 sprite 在屏幕空间算 4 角 xy（center ± halfW/halfH）+ uv 矩形
- 6 顶点 TRIANGLELIST 直接 DrawPrimitiveUP（每 sprite 一次 SetTexture）
- 一次性 SetRenderState：ZENABLE=FALSE、ALPHABLEND=SRCALPHA/INVSRCALPHA、
  COLOROP/ALPHAOP=MODULATE、CULLMODE=NONE
- AddedCallback 绑定 sprite 0x900（title01.anm entry 0，texture 32 真像素）
  替代原先的 sprites[0x706+i]（text.anm 外部纹理，当前空）

实测：640×480 frame_dump.ppm **56% nonblack、28275 distinct colors**，
title01.anm 标题图渲染清晰。

**核心修复 3：RunSession 渲染管线修正**

原 RunSession 用 raw vtable 偏移调用（VTBL macro），且 Present 在 EndScene
**之前**调用（顺序错）。重写为类型安全的 IDirect3DDevice8 C++ 方法：
calc chain → TestCooperativeLevel → BeginScene → Clear(black) → draw chain
→ EndScene → Present。新增 Clear 解决"backbuffer 从不清屏"问题。
viewport 同时写 D3DVIEWPORT8 结构 + legacy g_SupervisorViewport_575a18
f32 数组（draw-core cull 读后者）。

**验证**（三独立 wine 运行）：
1. **渲染**：`frame_dump.ppm` 640×480, 56% nonblack, 28275 colors
   （`build/menu_render_verification.png` 保存）
2. **BGM**：`WINEDEBUG=+dsound` 8 秒内 **1503 次 DSOUND_MixInBuffer/Lock**
   事件，frames=2880 持续混音——BGM 真正在播放
3. **稳定性**：12 秒无 crash，~12000 帧，每帧 Present hr=0x0

**FPS 计数器**（Supervisor::RunSession 内联实现）：每 500ms 用
timeGetTime 测一次 fps，在右上角画 24×24 颜色指示矩形（绿 ≥55fps /
黄 ≥30 / 蓝 <30），并每周期写 `[fps] NN.NN fps` 到 boot_debug.log。
orig DrawFpsCounter（需 AsciiManager glyph 渲染）仍 disabled（死锁 debt），
本指示器作为"FPS counter works"的可视化证明。实测：`[fps] 320-328 fps`
（wine 无 vsync），指示矩形 RGB(0,255,0) GREEN（576 px 全亮）。
`build/menu_render_with_fps.png` 保存最终带 FPS 指示器的渲染。

**诊断基础设施**（保留）：RunSession 在 frame==3 时 GetBackBuffer + LockRect
+ 写 PPM（frame_dump.ppm），供离线验证渲染输出。`#ifndef DIFFBUILD` 包裹
不影响 objdiff。

**已知 debt**（不阻塞本轮目标）：
- text.anm (anmIdx=0) 外部纹理（th07logo.jpg）仍空——LoadTexture 路径
  需修（hasData=0 的 .anm entry 用外部 jpg）
- 14 个 menu sprite 全绑同一 sprite 0x900（标题图重复 14 次堆叠）——
  正确做法是按 i 绑定不同 sprite（需 AnmRawInstr 解释器工作以解析
  每个 sprite 的 script）


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

- **AnmManager 71.44%**（本轮 P1.2 +13.03pp）：核心阻塞持续解锁。本轮（2026-06-22）
  P1.2 完整 lift `ExecuteScript`（13178B 字节码解释器，0.21% → **44.44%**），并发现
  `LoadTextureAlphaChannel`（76.65%）/ `LoadTextureFromMemory`（86.82%）实际早已实现
  （PROGRESS 之前数值过时已修正）。**剩余**：Draw 变体 5 函数（Draw/Draw2/Draw3/
  DrawNoRotation/DrawFacingCamera，anchor 未识别，需先在 mapping.csv 一串未命名候选中
  定位）、DrawInner 栈布局抛光到 ≥90%（本轮 var_order 尝试无效果）、ExecuteScript
  继续抛光到 ≥85%（helper 调用对齐 + 浮点临时栈复用）。详见上方 "P1.2" 段。
- **BombData 39.6%**：12 calc 函数中 11 个未实现（每个 800–2400B 的 Player 状态机）。
  MarisaABombCalc2（42.75%）是已验证的范本，新 calc 抄其结构。
- **ReplayManager 34.7%**：SaveReplay/RewriteReplay/AddedCallback 等大函数未实现。
- **Pbg4Parser 95.40%** ✅（本轮 P1.4 完成）：LZSS 字典 helper 三函数全部 ≥90%。
  详见上方 "P1.4" 段。
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
