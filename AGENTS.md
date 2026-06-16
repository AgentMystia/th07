# TH07-RE 反编译重建项目 — Agent 指令

你是 TH07-RE 反编译重建项目的 Coding Agent（米斯蒂娅的助手，沟通可爱亲切但能力专业）。这些指令 OVERRIDE 默认行为，必须严格遵守。

## 项目身份
- **目标**：以 th06 官方源码为标尺，反编译重建东方妖妖梦 `th07.exe`（1721 函数）。
- **验收**：objdiff 字节级指令匹配（match_percent），每模块平均 ≥90%，最终全量 ≥90%。**全功能交付，可替换原 exe**——单个函数 ~90% 即收，不迭代死磕 100%。
- **编译器**：MSVC 7.0（VS.NET 2002），通过 wine 调用。`/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy`（**无 /G5 无 /Op 无 /GS**，与 th06 不同）。

## 工作目录与改动约束
- **仅限**项目目录 `/home/mystia/项目/TH07-RE` 和临时目录 `/tmp`。不触碰系统目录/家目录其他部分。
- 项目内删除/覆盖自主推进；系统目录/家目录改动先确认；方向性决策（选型/架构）先征询。

## 自主推进流程（每模块）
1. **分析 orig**：用 ghidra-mcp 的 `disassemble_function {address}`（权威，反汇编为准）和 `decompile_function`（参考，注意误导：地址当值/+1漏标/逗号表达式/switch vs CMP链）。
2. **写/改 C++**：参考 `src/Player.cpp`（最新精修范本，含全部 codegen 技巧实例）。新模块照此风格。
3. **编译**：`python3 scripts/build.py --build-type=objdiffbuild --object-name <Module>.obj`
4. **objdiff**：
   ```bash
   objdiff-cli diff -1 build/objdiff/orig/<M>.obj -2 build/objdiff/reimpl/<M>.obj -o /tmp/<m>.json --format json-pretty
   ```
   match% 在 `left.symbols[].match_percent`（snake_case）。reimpl codegen 对比用 `objdump -d -M intel build/objdiff/reimpl/<M>.obj`。
5. **达 90% 后**：`git commit` + 更新 `PROGRESS.md`。

## 重新导出 orig obj（改 mapping.csv 后才需要，耗时 ~1min）
```bash
rm -rf /tmp/th07_new && mkdir -p /tmp/th07_new && /opt/ghidra/support/analyzeHeadless /tmp/th07_new TH07 \
  -import th07/th07.exe -scriptPath scripts/ghidra \
  -postscript ImportFromCsv.java config/mapping.csv \
  -postscript ExportDelinker.java config/ghidra_ns_to_obj.csv build/objdiff/orig -overwrite
```
**mkdir 必需**（analyzeHeadless 不建父目录）。mapping.csv 避免含空格/反引号的名字（如 `scalar deleting destructor`）和 UNKNOWN size，否则 ImportFromCsv 中断。

## 6 大 codegen 匹配技巧（详见记忆 codegen-objdiff-tricks）
1. 内联 asm 用 `#ifndef DIFFBUILD` 包裹（objdiff build 不定义 DIFFBUILD 走 asm，SDL/exe 走 C）
2. 跨模块 __thiscall callee 用 stub struct 方法（`((Stub*)0xADDR)->Method(...)` → PUSH/MOV ECX/CALL），不用 extern __fastcall
3. static inline 辅助在 /Od 下不内联 → 改 `#define` 宏
4. 去局部缓存匹配 orig 重读全局
5. objdiff 对外部 CALL/reloc 宽容；atan2 用 extern "C" 符号 call 匹配 orig call imm
6. IMUL/特殊寻址用 `#ifndef DIFFBUILD __asm` 逐条翻译

## Session 2026-06-16 新增 codegen 发现（Supervisor/BombData，已验证）

### 字符串 rdata reloc 匹配（关键！）
orig 用绝对地址 push rdata 字符串（`PUSH 0x4980d0` = "dummy"、`PUSH 0x496c1e` = ""）。
**字面量 `"dummy"` 会生成不同 reloc → 不匹配**。正确做法：
```cpp
extern "C" char g_DummyStr[];   // DAT_004980d0 "dummy" — 留 undefined，objdiff build 只编 .obj 不链接
#define DUMMY_STR ((char *)g_DummyStr)
extern "C" char g_EmptyStr[];   // DAT_00496c1e "" (NUL byte)
```
**`0x496c1e` 不是 NULL**——它指向一个 NUL 字节（空字符串），`PUSH 0x496c1e` ≠ `PUSH 0`（opcode 不同：`68 ..` vs `6a 00`）。

### f32 字段访问：勿 i32 强转
orig 对 Player-bomb AnmVm 的 posX/Y/Z（@ +0x1c8/+0x1cc/+0x1d0）做 `FLD/FADD/FSTP`（直接 f32）。
写 `(i32)(*f32 + ...)` 会插 `fistp`/int roundtrip → **顺序与 orig 完全不同，整体偏移**。
正确：`*(f32 *)(anm + 0x1c8) += *reinterpret_cast<f32 *>(0x0062f864);`（raw f32）。

### 循环体必须手动展开
orig 对 4-sub-sprite 绘制**全展开**（4 份相同 block）。写 `for (s = 0; s < 4; s++)` 会致
整个循环偏移 → match% 从 76% 掉到 25%。必须写 4 份独立 block（可用 `{}` scope 隔离同名局部）。

### AnmManager 单例调用 stub-method-on-global
`AnmManager::Draw2/Draw3(this=[0x4b9e44], AnmVm*)` @ 0x444f770/0x444f9a0。
正确：`(*(AnmMgrStub **)0x4b9e44)->Draw3(anm)` → MSVC 生成 `MOV ECX,[0x4b9e44]; PUSH anm; CALL`。
**勿用 `extern __fastcall`**（第 2 条通用规则）。

### Dead-store / frame-size 不可控（PlayMidiFile 0% 根因）
orig PlayMidiFile 保留 dead `d2 = d` store（写入但不读），frame 0x120；MSVC /Od **消除 dead store**
致 frame 0x11c → 整函数偏移 → 0% match。已试 volatile/self-assign 均无效。**此类函数需
`#ifndef DIFFBUILD __asm` 强制局部布局**（第 6 条技巧），或接受 0%。
**诊断法**：objdiff 显示 0% 但两边指令数相同 → 99% 是 frame/local-offset 偏移导致。

### fcomp flag-branch codegen 限制（FadeOutMusic）
同 `TEST AH,0x41` 后 orig 用 `JNZ`，MSVC 对 `<=` 可能生 `JP`（不同 opcode）。
条件表达式重组（`&&`、`!(>)`）有时能切到 JNZ，但不保证。接受 ~69% 或 inline asm。

### mapping.csv 新增模块函数（本 session）
- **BombData**：24 个回调（12 calc/draw 对）已加，命名 `th07::BombData::{Char}{Shot}{Calc/Draw}`，
  地址 0x408710..0x40e280，全部 `__fastcall` ECX=Player*。
- **ItemManager**：OnUpdate @ 0x432990（0x10c9 字节）+ RegisterChain @ 0x432eda 已加。
- **BulletData**：命名空间下**无函数**（纯 .data 表）→ ExportDelinker 产空 obj。需数据符号方案（未实现）。

### 重新导出已生效
mapping.csv 改后已重导 orig obj（BombData.obj 41816B、ItemManager.obj 11794B 已在 build/objdiff/orig/）。

## Session 2026-06-16 (续 2) 新增 codegen 发现（Supervisor 精修，已验证）

### MSVC /Od 局部布局硬限制（PlayMidiFile 0% 根因）
MSVC 7.0 /Od 对函数内**所有非数组标量局部变量**（无论声明顺序/作用域/volatile）
一律分配到帧底（`[ebp-0x4]`, `[ebp-0x8]`, ...），数组单独从帧顶向下分配。
orig 有时把标量放在数组上方（如 midi@`[ebp-0x10c]` 在 buf@`[ebp-0x108]` 之上），
MSVC 对任何 C++ 写法都做不到——唯一解是 inline asm（但 asm call C 符号报 C2415）。
**诊断法**：objdiff 0% 但两边指令数相近 → 检查 frame size，若 reimpl 帧与 orig 不同
且 orig 标量在数组上方 → 此限制。

### memset/memcpy intrinsic 生成 rep stosd / rep movsd（LoadConfig 关键）
`memset(addr, 0, N*4)` 在 /Oi 下生成 `push N; pop ecx; xor eax,eax; mov edi,addr; rep stosd`。
`memcpy(dst, src, N*4)` 生成 `mov esi,src; mov edi,dst; rep movsd`。
小 memcpy（18B）生成 `movsd x4 + movsw`（展开）。**手写 for 循环不匹配**，必须 intrinsic。
注意 `extern "C" void *memset/memcpy(...)` 必须文件级声明（C2598）。

### Win32 API 必须直接 call（不走 wrapper）
orig 对 CreateFileA/ReadFile/CloseHandle/timeGetTime/QueryPerformanceCounter 都是
`call DWORD PTR ds:[IAT]`。若用 wrapper（`CreateFileA_th07` __fastcall），MSVC 生成
`mov edx; mov ecx; call wrapper` —— 完全不匹配。**正确**：直接调 `<windows.h>`/`<mmsystem.h>`
真实 API（MSVC 生成 `call [__imp__Api]`，objdiff 容忍 reloc）。

### opts 位检查每次重读全局（LoadConfig apply_opts）
orig 对 `this+0x14c` 每个位测试都 `mov eax,[this]; mov eax,[eax+0x14c]; shr; and; test`。
C++ 缓存 `u32 opts = ...` → 不匹配。每个 if 写 `(*(u32*)((u8*)this+0x14c) >> N & 1)`。

### inline asm 能力边界（MSVC 7.0 实测）
- `__declspec(naked)` + `__asm {}` 可声明，但 **`call <Cfunc>` 报 C2415**。
- **`call dword ptr [fp]` 可用**（fp 是函数指针局部）——生成 `call *[ebp-X]`。
- 绝对地址 store `mov [0xADDR], imm` 在 naked asm 也报 C2415。
- 复杂函数（状态机/vtable call）纯 asm 不可行；必须 C++ + 精确表达式。

### AutosaveScore __thiscall：stub-method-on-singleton
orig `MOV ECX,0x575950; PUSH x3; CALL` = __thiscall ECX=g_Supervisor + 3 stack args。
`struct SupAutosaveStub { i32 AutosaveScore(char*,i32,i32); };` +
`(*(SupAutosaveStub*)0x575950).AutosaveScore(...)` 精确匹配。

### Supervisor 续作检查点（avg 68.25%，3×≥90%）
1. **PlayMidiFile 0%**：inline asm + 函数指针 call（`call dword ptr [fp]`）强制 midi@[ebp-0x10c]。
2. **LoadConfig 43%**：buf/read1/handle 槽位偏，需更精细声明实验或 asm。
3. **DrawFpsCounter 73%**：orig 纯 FPU 栈，reimpl f32 局部致 frame 偏大。重写 FPU 栈。
4. **DeletedCallback 54% / SetupDInput 57%**：外部 stub 调用序列复杂。
5. 已 ≥90% 不动：OnDraw 100%、TickTimer 97.95%、PlayAudio 90.5%。

## MSVC 7.0 硬限制
- 不支持 `nullptr`（C++11）→ 用 `0`（`ptr_field = 0` 编译成 `and [mem],0`，匹配 orig 零初始化 idiom）。
- 不认 UTF-8 中文注释（无 BOM 也无效）→ **源码注释必须英文**。
- callback `__fastcall`（th07 第四大差异，非 th06 的 __cdecl）。
- `extern "C"` 必须文件级（C2598）；inline asm 不接受 `call imm`（C2415）。

## 可移植性原则（硬约束）
不为 objdiff 100% 牺牲 SDL2 port。内联汇编照搬 th06（仅约 9 处，用 `#ifndef DIFFBUILD` 包裹）。跨模块单例地址用 `#ifdef DIFFBUILD`（地址立即）/ `#else`（extern 真实对象）双分支。

## 关键参考文件
- `PROGRESS.md` — 详细进度、每模块 match%、技术发现（**必读**）
- `src/Player.cpp` — 精修范本（stub struct/宏/asm/atan2 符号 call）
- `src/GameManager.cpp` — CALL 密集大函数的 C++ 实现范本
- `config/mapping.csv` — ghidra 符号→地址映射
- `objdiff.json` — objdiff 配置
- `src/ZunMath.hpp` — sincos/atan2 等 asm wrapper 参考

## 大函数教训
单 agent 啃 2000+字节函数会 STALL。大函数必须手动 ghidra 逆向（disassemble_function 权威）。小函数/结构体分析可并行 workflow。

## 续作检查点（Session 2026-06-16 结束状态）
新会话**第一件事读 PROGRESS.md** 的对应模块章节，然后从这里接：

### Supervisor（13/14 函数 objdiff，avg 65.8%，3×≥90%）
**下一步优先级**：
1. **PlayMidiFile 0%**：frame-mismatch（0x11c vs orig 0x120，dead d2 store）。尝试 inline asm 强制 `[ebp-0x10c]` 局部，或重写为完全匹配 orig 字节循环结构。参考本 session 死磕记录。
2. **OnUpdate 44% / LoadConfig 28%**：大状态机/配置函数，逐 block ghidra 反汇编精修。
3. **AddedCallback (0x45b) 未实现**（None）——D3D 设备 + 纹理加载 + MidiOutput 创建，FUN_00438986。
4. 已 ≥90% 的：OnDraw 100%、TickTimer 97.95%、PlayAudio 90.5%——不动。

### BombData（24 函数，avg 39.6%，12 draw 已完成）
**下一步**：实现 12 calc（每个 0xe0~0x960 字节）。按 size 从小到大：
- 已做：MarisaABombCalc2 (0x4e0) 43%（timer state machine 范本，**抄它的结构**）。
- 待做（按地址）：ReimuCBombCalc 0x408710(0x700)、ReimuABombCalc 0x4091b0(0x7e0)、MarisaABombCalc 0x409dd0(0x4b0)、
  MarisaBBombCalc 0x40a3a0(0x310)、SakuyaABombCalc 0x40a7c0(0x3e0)、SakuyaBBombCalc 0x40af10(0x6c0)、
  ReimuBBombCalc 0x40b7d0(0x4d0)、YoumuABombCalc 0x40be20(0x340)、ReimuABombCalc2 0x40c2e0(0x690)、
  YoumuBBombCalc 0x40ca50(0x960)、SakuyaABombCalc2 0x40da80(0x800)。
**calc 通用骨架**（MarisaABombCalc2 已验证）：
```
if (curTime >= duration) { EndSpellcard(); reset; AnmVm_Die; return; }
if (curTime != prevTime && curTime == T0) { init block }
if (curTime != prevTime && curTime == Tn) { ... }
... 状态机分支 ...
final: AnmVm_ExecuteScript loop; state=3; prev=cur; TickTimer
```
**Player 结构体偏移**（calc/draw 共用，已验证）：
- +0x16a20 isInUse, +0x16a28 duration, +0x16a30 timer.prev, +0x16a34 timer.subFrame, +0x16a38 timer.cur
- +0x16a4c perBombState[0]（8 entries，stride **0x1428 字节 = 0x50a i32**）
- +0x16a08 invulnTimer, +0x23f0/f4 moveSpeedMultDuringBomb, +0x2408 playerState(u8)
- +0x930 posCenter(D3DXVECTOR3), +0x9dc..+0x9f8 bombRegionPos/Size/Dmg（0x20 stride）
- perBombState entry 内：[0x000]active, [0x010]angle, [0x014/8/1c]spriteIdx/x/y, [0x1b8]AnmVm(base),
  AnmVm 内 +0x8 currentAngle, +0x1c0 flags(DWORD), +0x1c8 posX, +0x1cc posY, +0x1d0 posZ, +0x230 delta(D3DXVECTOR3)
**calc 依赖 extern**（已声明在 BombData.cpp，按地址用）：00433a90/42868d/4084f0/408610/4277a0/404f30/44b310/450d60/43958d/427b21/4418b0/44c930

### BulletData（0%，结构阻塞）
纯 .data 模块，无函数。**要 90% 必须先实现数据符号导出方案**：
ExportDelinker 只导出 namespace 内**函数/数据符号的 body**。需把 g_BombData-style 表的 data labels
放进 th07::BulletData namespace（Ghidra 里手动 Disassemble+CreateData+label，或扩展 ImportFromCsv 支持 data）。
这是**方向性决策**，新会话先征询用户。

### ItemManager（0%，orig obj 已导出）
OnUpdate @ 0x432990（0x10c9 字节，switch 重度 item 行为机）。**单体巨型函数**，
新会话需手动 ghidra 逐 case 反汇编（disassemble_function 权威），分 block 移植。
参考 src/GameManager.cpp 的 CALL 密集大函数实现范本。

## 终止条件（遇硬阻塞才停）
- 需重新导出 orig obj 且非自主可控
- 结构体偏移矛盾无法仅靠项目内修改解决
- 长时间无有效进展
