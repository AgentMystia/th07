# TH07-RE 反编译重建进度

最后更新：2026-06-14

## 已 objdiff 验证模块（11 模块，~95 函数，21×100%）

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
| zwave | 26 函数，**5×100%**，平均 92.3%（CSound/CWaveFile/CStreamingSound）|
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
