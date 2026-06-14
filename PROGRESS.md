# TH07-RE 反编译重建进度

最后更新：2026-06-15

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

### GameManager（最大模块，首批 3 函数 + 结构体基础）
- **g_GameManager @ 0x626270**（~1.4MB 含 Item[1100]×0x288 等实体数组）
- **g_Chain @ 0x626218**（全局 Chain 控制器，AddToCalc/DrawChain 的 this）
- 嵌入式链节点：updateChainNode@+0x9644 / drawChainNode@+0x9664（ChainElem 0x20）
- +0x8 = **scoreSub 指针**（堆分配，ScoreSub.guiScore@+0x0 / .score@+0x4，CutChain clamp 到 999999999）

| 函数 | match% | 备注 |
|---|---|---|
| OnDraw | **100%** | trivial（unk_93dc=2）|
| CutChain | **97.4%** | Cut 两节点 + score clamp |
| RegisterChain | 76.1% | objdiff 符号命名限制（见下）|

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
