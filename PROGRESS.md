# TH07-RE 反编译重建进度

最后更新：2026-06-16

## ★Session 2026-06-16: ghidra namespace 工具链修复 + Supervisor 部分实现★

### 🔑 重大突破：修复 ghidra namespace 导入（解锁全部 40 模块导出）
- **根因**：之前的 `scalar deleting destructor` 中断问题已不存在（mapping.csv 已清洁），但 Supervisor 等 22 个模块的函数从未加入 mapping.csv，导致 th07:: namespace 全部为空、ExportDelinker 无法导出这些模块 obj。
- **修复**：补全 Supervisor 14 函数到 mapping.csv（地址+大小全部 ghidra 实读验证），重跑 ImportFromCsv + ExportDelinker headless 流程（36 秒）。
- **成果**：orig obj 从 17 个增至 18 个（新增 Supervisor.obj 26KB）。40 模块的 namespace 导出现在可按需补 mapping.csv 行即解锁。

### Supervisor（第 16 模块，部分实现 7/14 函数，平均 **48.5%**，WIP）
orig 26.4KB / 14 函数。从语法损坏的初稿重写为可编译的 C++（移除中文注释——MSVC 7.0 不认）。
| 函数 | match% | 备注 |
|---|---|---|
| TickTimer | **97.95%** | 去局部缓存 + raw offset this+0x178 匹配 orig 重读 |
| RegisterChain | 74.91% | g_Chain.CreateElem/AddToCalcChain；struct offset 已修正 |
| StopAudio | 62.17% | 音频路由（MIDI/WAV 分支） |
| OnDraw | 57.73% | 调 DrawFpsCounter(1) |
| FadeOutMusic | 26.75% | framerate 调整 + SoundPlayer |
| ReadMidiFile | 18.24% | musicMode 路由 |
| DrawFpsCounter | 1.83% | 仅早期 return guard；大函数体待实现 |

### 关键 struct 修正（影响 Supervisor 全模块）
- **DIDEVCAPS_FAKE**：14 u32 → 11 u32（th07 不含 FirmwareRevisionH/HardwareRevisionH）
- **D3DPRESENT_PARAMETERS_FAKE**：fields[14] → fields[13]（th07 少一个字段）
- 这两处修正让 calcCount/wantedState/curState 从 +0x160/+0x164/+0x15c 回到正确的 +0x150/+0x154/+0x158

### 待续（Supervisor 剩余 7 函数）
- OnUpdate (0x6aa 字节，状态机大函数)
- AddedCallback (0x45b) / DeletedCallback (0x20d)
- SetupDInput (0x28f, DirectInput 初始化)
- LoadConfig (0x519, GetPrivateProfileInt 配置加载)
- PlayAudio (0xf0) / PlayMidiFile (0x111)
- DrawFpsCounter 大函数体（双路径 fps 测量 + slow% 累积）

### EffectManager（第 17 模块，14 函数 objdiff，平均 **78.4%**，7×90%+）
orig 10.2KB / 14 函数（AddedCallback@0x41cdd4 ghidra overlap 未导出，3 函数待补）。
通过读取 orig g_Effects 表（0x49efc0, 34 entry）发现真实 callback 地址，修正 4 个错地址。
| 函数 | match% |
|---|---|
| Reset | **99.92%** |
| EffectCallbackStill | **99.93%** |
| EffectCallbackRandomSplashBigInit | **99.23%** |
| EffectUpdateCallback4Init | **99.90%** |
| EffectUpdateCallback4 | **99.19%** |
| SpawnParticlesWithVelocity | **95.28%** |
| OnUpdate | **90.46%** |
| SpawnParticles | 88.88% |
| SpawnParticleAt | 83.10% |
| EffectCallbackAttractInit | 79.25% |
| EffectCallbackAttract | 45.76% |
| EffectCallbackAttractSlow | 44.36% |
| OnDraw | 36.46% |
| EffectManager (ctor) | 35.40%（body 完全不同，需重写）|

### Supervisor（8 函数 objdiff，平均 **57.0%**）
OnDraw **100%**（__fastcall 修正）, TickTimer **97.95%**, StopAudio 84.50%（thiscall stub）,
RegisterChain 74.91%（CALL 密集，符号命名限制上限）, OnUpdate 44.45%（CALL 密集）,
FadeOutMusic 33.29%, ReadMidiFile 18.24%, DrawFpsCounter 2.48%（仅 early-return stub）。

### EffectManager 关键修复
- **ParticleEffects enum**：从 25 扩展到 34 成员（缺 idx 4-11/14-15，致 g_Effects 表 C2078）
- **Effect.hpp**：移除零大小 pad2 数组
- **static 成员限定**：g_Effects 表用宏别名 `#define X (EffectManager::X)` 限定 static 回调

### codegen 复用技巧（已验证）
- **去局部缓存匹配 orig 重读**：TickTimer 0%→97.95% 仅靠此（每次直接读 `*(f32*)((u8*)this+0x178)` 而非缓存到局部）
- **raw offset 绕过 struct 偏移错误**：当 hpp struct 布局与 orig 不符时，用 `*(T*)((u8*)this+OFF)` 直接访问
- **🔑🔑 __fastcall 是 static callback 的命脉**：th07 static chain callbacks（OnUpdate/OnDraw/AddedCallback/DeletedCallback/DrawFpsCounter）都是 `__fastcall`（Supervisor* in ECX），但 MSVC C++ static 方法默认 `__cdecl`。**必须在 hpp 声明和 cpp 定义都加 `__fastcall`**，否则参数走栈而非 ECX，match 暴跌 40%+。Supervisor OnDraw 57%→100% 仅靠此。
- **🔑🔑🔑 读取 orig 数据表发现真实函数地址**：g_Effects 表（34 entry × 12 bytes）在 .data 段，含所有 effect callback 的真实 VA。用 ghidra `inspect_memory_content` 读 0x49efc0，解析每 entry 的 update/init 指针，即可定位所有 callback 地址。EffectManager EffectUpdateCallback4 0%→99.19% 仅靠此（原映射 0x41ad10 错，真实 0x41a750）。
- **🔑🔑 thiscall callee 用 struct-method stub**：跨模块 __thiscall 调用（如 MidiOutput::SetFadeOut），声明 `struct MidiOutput { ZunResult SetFadeOut(u32); }` + `(*(MidiOutput**)0x575acc)->SetFadeOut(...)` 生成 `mov ecx,[singleton]; call`。Supervisor StopAudio 62%→84.5% 仅靠此。
- **objdiff 符号命名限制**：CALL 密集函数（RegisterChain 74.9%, OnUpdate 44%, SpawnParticles 88.9%）即使指令 0 差异也卡在符号名层面——orig reloc 用 `th07::X::Y` mangled 或 `dir32 DAT_addr`，reimpl 用不同符号名。这是当前 objdiff 的固有限制，需在 objdiff 层面做符号映射或重命名 reimpl 全局匹配 orig DAT_ 才能突破。

### 本 session 累计成果
- **工具链突破**：修复 ghidra ImportFromCsv（补 mapping.csv），orig obj 17→19 模块，217 函数地址锁定（12.6% of 1721）
- **新增 2 模块 objdiff 跟踪**：Supervisor (8 函数) + EffectManager (14 函数) = 22 函数
- **9 个函数 ≥90%**：Supervisor OnDraw 100%/TickTimer 97.95%; EffectManager Reset 99.92%/Still 99.93%/SplashBigInit 99.23%/UpdateCB4Init 99.90%/UpdateCB4 99.19%/SpawnPartWithVel 95.28%/OnUpdate 90.46%

## ★Session 2026-06-15 (夜间长程): ScreenEffect 新模块 + AsciiManager 新模块 + FileSystem 精修★

### ScreenEffect（第 14 模块，13 函数，平均 **89.55%**，4×100%）
orig 7.9KB / 13 函数。从无到有完整实现。
| 函数 | match% |
|---|---|
| DrawFadeOut | **100%** |
| DrawFlickerFade | **100%** |
| AddedCallback | **100%** |
| Clear | 97.54% |
| SetViewport | 97.00% |
| DrawFadeIn | 98.06% |
| DeletedCallback | 86.00% |
| ShakeScreen | 82.50% |
| RegisterChain | 81.55% |
| CalcFadeIn | 83.45% |
| CalcFadeOut | 83.24% |
| DrawSquare | 79.82% |
| CalcFlickerFade | 75.35% |

### AsciiManager（第 15 模块，16 函数，平均 **69.95%**，8 函数 ≥80%）
orig 32.9KB / 16 函数（含 StageMenu 委托）。核心函数已高匹配。
| 函数 | match% |
|---|---|
| AddFormatText | **99.78%** |
| CutChain | **97.00%** |
| AddedCallback | 90.19% |
| InitializeVms | 88.34% |
| CreatePopup1 | 87.11% |
| CreatePopup2 | 87.25% |
| OnDrawMenus | 86.81% |
| OnUpdate | 86.44% |
| AddString | 84.26% |
| DeletedCallback | 80.00% |
| OnDrawPopups | 69.40% |
| InitializeMenuVms | 61.11% |
| RegisterChain | 58.05% |
| AsciiManager (ctor) | 39.51% |
| DrawStrings | 2.64% (待大函数逆向) |
| DrawPopups | 1.34% (待大函数逆向) |

### FileSystem 精修（RawWriteFile 64% → **99.03%**）
移除 `result` 局部变量 + early-return 风格，完美匹配 orig 的 `OR EAX,-1`/`MOV EAX,-2`/`XOR EAX,EAX` 控制流。FileSystem 平均 **95.6%**。

### mapping.csv 修复
第 119 行坏行（ValidateReplayData 和 SoundPlayer::SoundPlayer 被合并）拆分为正确的两行。纯文本修改，不触发 orig 重导出。

### 3 大 codegen 发现（本 session）
1. **objdiff 最优模式 = 直接绝对地址 cast**：`(*(T**)0xADDR)` 或 `(*reinterpret_cast<T*>(0xADDR))`（直接对象）。让 MSVC 生成 `mov reg,[addr]` 匹配 orig 的 `mov reg,[DAT_addr]`。通过 g_Supervisor 结构体偏移访问会多一层间接（先取 g_Supervisor 再 +off），不匹配。ScreenEffect Clear 从 67%→97% 仅靠此修正。
2. **D3D 方法调用用 `(*(IDirect3DDevice8**)0x575958)->Method()`** 宏：每次调用重新读地址，不缓存到局部变量。MSVC COM thiscall 生成 `mov ecx,[addr]; mov edx,[ecx]; push ecx; call [edx+off]` 精确匹配 orig。
3. **early-return 匹配 orig 控制流**：orig 的错误路径用 `OR EAX,-1`/`MOV EAX,-2`/`XOR EAX,EAX` 直接返回，不用 result 局部。C++ early-return 风格（每分支独立 return）完美匹配。RawWriteFile 64%→99% 仅靠此。

### 仍待优化（非阻塞）
- AsciiManager DrawStrings(0x440)/DrawPopups(0x7d9) 大函数：需完整逆向，含字符串渲染循环 + point label 颜色逻辑
- Player ScoreGraze(71.69%)/CalcItemBoxCollision(61%)：浮点栈布局，orig 用大量 f32 临时 + 三层 copy，最难
- SoundPlayer 平均 75.9%：BGMThread/ProcessSoundQueues 大函数

## ★Session 2026-06-15 (续): Player 精修 90.79%★

Player 模块 9 函数从 agent 初稿平均 62.7% 反汇编级精修到 **平均 90.79%**。

| 函数 | match% | 备注 |
|---|---|---|
| StartFireBulletTimer | **100%** | ZunTimer 重置 current/subFrame/previous 倒序 + cur 局部先读 |
| OnDrawLowPrio | 99.50% | 保留 C++（extern Player_DrawBulletExplosions）|
| RegisterChain | 99.15% | g_Chain 成员方法 + raw[0xb7e5c..] 字段（=DAT_00575934）+ 不缓存 calc/draw 局部 |
| Die | 99.02% | stub struct thiscall（Effect/Sound/Game 方法）|
| DeletedCallback | 98.47% | ANM_MGR 宏 + 直接 cmp（不缓存）+ #ifndef DIFFBUILD asm 重建 IMUL Gui 计数 |
| CutChain | 96.84% | g_Chain.Cut + raw[0xb7e5c] 字段 |
| AngleToPlayer | 91.36% | PI/2 全局 [0x498a9c] + atan2 extern "C" 符号 call（objdiff 容忍外部 call）|
| ScoreGraze | 71.69% | 浮点栈布局差异（待 orig 临时变量结构重建）|
| CalcItemBoxCollision | 61.08% | 浮点栈布局差异（orig 大量中间临时，最难）|

### 5 大 codegen 发现（本 session）
1. **objdiff build 不定义 DIFFBUILD**（AnmManager.cpp 用 `#ifndef DIFFBUILD`）→ 内联 asm 用 `#ifndef DIFFBUILD` 包裹（objdiff 走 asm，SDL/exe 走 C）。MSVC asm 项目可接受（ZunMath.hpp 无条件用 fsincos），SDL port 也是 MSVC（非 GCC）
2. **跨模块 thiscall 用 stub struct 方法**：`struct EffectManagerSpawn { void SpawnEffect(...); };` + `((Stub*)0x12fe250)->method(args)` 生成 `PUSH args; MOV ECX,this; CALL`（精确 thiscall codegen）。extern "C" __fastcall 第二参数走 EDX 不匹配 orig stack
3. **static inline 辅助函数 /Od 不内联**！返回引用的 `g_AnmManagerFiles()` 生成多余 call → 改宏 `#define ANM_MGR (...)` 强制展开
4. **局部变量缓存 vs orig 重读**：orig 每次 `MOV reg,[全局]`，C++ 缓存到 [ebp-x] 不匹配 → 直接表达式或重读（RegisterChain calc/draw 不存局部、DeletedCallback optUnfocused 直接 cmp）
5. **objdiff 对外部 CALL/reloc 宽容**：Die 99% / RegisterChain 99% 即使所有单例地址是 reloc（extern 变量）仍匹配；atan2 `call atan2_disp_0048bcaa`（extern 符号）匹配 orig `call 0x48bcaa` 无需 mapping.csv

### 仍待优化（非阻塞，平均已 90.79%）
- ScoreGraze/CalcItemBoxCollision 浮点栈布局：orig 用 f32 临时 + D3DXVECTOR3 + copy 三层，MSVC /Od 栈分配难精确重建。需逐变量模拟 orig 声明顺序

## ★Session 2026-06-15: Controller 验证 + GameManager 基础★

### Controller（第 13 模块，7 函数，平均 **98.0%**，5×100%）
| 函数 | match% |
|---|---|
| GetInput | **99.9%**（1904B，含 win32+DInput 全部 key check）|
| ResetKeyboard | **99.6%** |
| GetJoystickCaps | **98.8%** |
| GetControllerInput | 94.1% |
| GetControllerState | 93.3% |
| SetButtonFromDirectInputJoystate | **100%**（83.8%→100%，见下）|
| SetButtonFromControllerInputs | **100%**（83.8%→100%）|

地址：GetJoystickCaps@0x430290 / SetButtonFromDirectInputJoystate@0x4302f0 /
SetButtonFromControllerInputs@0x430370 / GetControllerInput@0x4303f0 /
GetControllerState@0x4309c0 / GetInput@0x430b50 / ResetKeyboard@0x4312c0

### GameManager（最大模块，6/6 函数全部完成，平均 **89.1%**）
- **g_GameManager @ 0x626270**（sizeof 0x9700；Item[1100] 在 ItemManager @0x575c70，不在此）
- **g_Chain @ 0x626218**（全局 Chain 控制器）
- 嵌入式链节点：updateChainNode@+0x9644 / drawChainNode@+0x9664
- +0x8 = scoreSub 指针（堆分配 ScoreSub 0xC8）；+0x4 = playerSub（0x38）

| 函数 | match% | 备注 |
|---|---|---|
| OnDraw | **100%** | trivial（unk_93dc=2）|
| CutChain | **97.4%** | Cut 两节点 + score clamp |
| DeletedCallback | **94.6%** | MIDI teardown + release cascade |
| OnUpdate | **90.6%** | 2303B 每帧核心逻辑（暂停/rank/score平滑）|
| RegisterChain | 76.1% | objdiff 符号命名限制（CALL 密集）|
| AddedCallback | 75.8% | 2726B 分配器，CALL 密集 plateau |

9 函数地址全锁定（mapping.csv）：RegisterChain@0x42f3c5 / CutChain@0x42f45d /
OnUpdate@0x42d8d5(2303B) / OnDraw@0x42e1d4 / AddedCallback@0x42e83e(2726B) /
DeletedCallback@0x42f2e4 / **OnItemUpdate@0x432990(4298B,Item 调度器)** /
CalculateChecksum@0x42d7be / IsGameActive@0x42ad66

### 关键技术发现（本 session）
1. **MSVC 7.0 不支持 `nullptr`**（C++11，2010 才有）→ 用 `0`。`ptr_field = 0` 编译成 `and [mem],0`（匹配 orig 的零初始化 idiom）
2. **objdiff 全局存储符号命名限制**：reimpl 用 `?g_GameManager@th07@@...(mangled)+offset`，orig delink 成 `DAT_xxxxxxxx`——同一地址不同名，objdiff 按字符串比操作数 → RegisterChain 类（多次 g_GameManager+offset 存储）被压低到 ~76%，但逻辑正确（同 Chain switch/Rng F32 限制类）
3. **callback 赋值需 C 风格 cast**：`(ChainCallback)OnUpdate`（typedef 是 `(*)(void*)`，方法取 `GameManager*`，MSVC 宽容但需 cast 对齐）
4. **🔑 movzx codegen 技巧**：orig 对 enum/int 参数用 `movzx word ptr [param]`（当 u16 加载）时，reimpl 用 `(u16)param` 而非 `param & 0xFFFF`——前者产生 movzx，后者产生 `mov+and 0xffff`。修复 Controller SetButtonFrom\* 83.8%→100%
5. **🔑🔑 GameManager 尺寸重大修正**：sizeof(GameManager)=**0x9700**（非 0x169570）。Item[1100]×0x288 数组**不在 GameManager**，在 **ItemManager 单例 @ 0x575c70**（OnItemUpdate 调用点 `MOV ECX,0x575c70` 为证）。OnItemUpdate(0x432990) 是 ItemManager 函数，已从 GameManager mapping 移除。g_GameManager 字段止于 +0x96ec。ScoreSub 确认 0xC8（但 IsGameActive 读 +0x1fbac 矛盾待解）

## 已 objdiff 验证模块（此前 12 模块，~110 函数，21×100%）

### ★Wave 10 AnmManager 结构体关键修正★
- **sizeof(AnmManager) = 0x17e560**（不是 0x2e4dc！后者是 vertexBuffer 字段偏移）
- sprites/scripts/spriteIndices = **2560**（非 th06 的 2048）
- anmFiles = **50**（非 128，anmIdx<0x32）
- 完整字段偏移表（sprites@0x60, textures[264]@0x282ac, scripts@0x28ef0, vertexBuffer@0x2e4dc, scratchRegion[0x150000]@0x2e534）
- 第 12 模块：CreateEmptyTexture 99.4%/ReleaseTexture 88.3%，ExecuteScript(11KB)/Draw 家族 stub 待 lift

### 完整验证（100% 或 99%+）
| 模块 | 结果 |
|---|---|
| Rng | GetRandomU16/U32 = **100%**, F32 = 87%（i64 零扩展 + 符号名 objdiff 限制）|
| utils | DebugPrint = **100%**, AddNormalizeAngle/Rotate/CheckForRunningGameInstance = 99% |
| Chain | **6×100%**（ChainElem 构造/析构, AddToCalcChain/Draw, Release），RunCalc/Draw 86%（switch jump table objdiff 符号限制）|
| GameErrorContext | Log/Fatal/Flush = 99.4-99.9% |
| ZunTimer | Increment/Decrement = 99.5%, NextTick = 90%（寄存器+符号名）|
| MidiOutput | **4×100%**（ReleaseFileData/ClearTracks/LoadFile/SetFadeOut），平均 92.2% |
| AnmVm | **3×100%**（Initialize/AnmVm/ResetInterpTimers）|

### 部分验证
| 模块 | 结果 |
|---|---|
| zwave | 26 函数，**5×100%**，平均 **96.0%**（CSound/CWaveFile/CStreamingSound 全近完整）|
| SoundPlayer | 13 函数，平均 75.9%（StopBGM 92.6%, BGMThread 88.8%）|
| ReplayManager | StopRecording 99.7%, DeletedCallback 89%, RegisterChain 84%（其余 stub 依赖下游）|
| FileSystem | OpenPath 92%, RawWriteFile 64%（错误路径实现不全）|

## 初稿待完善
- **AsciiManager/EffectManager/ScreenEffect**：待 AnmManager（结构体偏移错待修正）
- **Controller**：初稿，待 mapping + objdiff
- **Supervisor.cpp**：14 函数地址验证，含 stub extern（GameManager/AsciiManager/MidiOutput/SoundPlayer 等下游）
- **AnmManager**：12 函数 mapping，hpp 结构体偏移错（vertexBuffer@0x2e4dc 但 ZUN_ASSERT_SIZE 0x2e4dc 矛盾）需完全重做
- **BombData/BulletData/ItemManager**：待 Player（th07 嵌入 GameManager，Item 0x288 vs th06 0x144）
- **Player**：28 函数地址 + 结构 0xb7e78 验证，8 函数实现；agent 用 C 风格（Player_RegisterChain + extern C）不匹配 orig C++ mangle（th07::Player::），需重写 C++ 方法

## 5 大关键技术发现（th06→th07 编译选项差异）
1. **无 /G5**（Pentium 优化；th06 有，致 u16 用 xor+mov 而非 movzx）
2. **无 /Op**（浮点一致性；th06 有，致浮点中间值 fstp+lld 截断）
3. **无 /GS**（security cookie；th06 有，致栈缓冲函数多 cookie 指令 + 栈偏移+4）
4. **callback `__fastcall`**（th06 `__cdecl`；arg in ECX vs push）
5. **mapping.csv `scalar deleting destructor` 致 ImportFromCsv 中断**（含空格/反引号触发 InvalidInputException，后续模块 namespace 全未建）

最终编译选项：`/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy`（无 /G5 /Op /GS）

## 其他技术
- **#pragma auto_inline(off)**：/Ob1 全局保留（ZunTimer/Chain 内联匹配），但 AnmVm::AnmVm 不该内联 ResetInterpTimers → 局部禁
- **MSVC 7.0 不认 UTF-8 中文**（无 BOM 也无效）→ 源码注释必须英文
- **D3DXMatrixIdentity __fastcall**：orig pOut in ECX，需 `#define shadow` 屏蔽 d3dx8math.h 的 WINAPI 声明（移文件最前）
- **F32 i64 零扩展**：orig `FILD qword`（u32 零扩展成 i64），源码 `(i64)GetRandomU32() / 4294967296.0f`

## 待办（优先级）
1. **AnmManager 结构体重做**（ghidra 实读 0x2e4dc 布局）→ 解锁 ScreenEffect/AsciiManager/EffectManager
2. **Player 重写 C++ 方法**（Player:: 而非 Player_）→ 解锁 BombData/BulletData/ItemManager
3. **Supervisor.cpp 下游**（GameManager/AsciiManager/MidiOutput 等 stub 补全）
4. **GameManager 反编译**（th07 最大模块，含 Item/Enemy 调度）
5. **迭代低 match**：F32 xor ecx、Chain switch、RawWriteFile 错误路径、ProcessMsg 寄存器

## 构建命令
```
# 生成 build.ninja
python3 scripts/configure.py
# 编译单模块 reimpl obj
python3 scripts/build.py --build-type=objdiffbuild --object-name <Module>.obj
# 导出原版 obj（绕 ghidra-mcp 锁，无 -readOnly 建 namespace）
analyzeHeadless /tmp/th07_new TH07 -import th07/th07.exe \
  -scriptPath scripts/ghidra \
  -postscript ImportFromCsv.java config/mapping.csv \
  -postscript ExportDelinker.java config/ghidra_ns_to_obj.csv build/objdiff/orig
# objdiff
objdiff-cli diff -1 build/objdiff/orig/<M>.obj -2 build/objdiff/reimpl/<M>.obj -o /tmp/<m>.json --format json-pretty
# 函数级 match_percent 在 left.symbols[].match_percent
```
