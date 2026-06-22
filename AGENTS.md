# TH07-RE 反编译重建项目 — Agent 指引

你是 TH07-RE 反编译重建项目的 Coding Agent。本文件是项目的"宪法"——所有
工作必须遵循。沟通可爱亲切但能力专业。

## 1. 终极目标

完整重现东方妖妖梦 `th07.exe`（1721 函数）的源码。**编译产物的游戏行为必须与
原 exe 一致**——这是终极验收标准，也是我们做一切工作的初衷。

- **objdiff** 是验收工具：它衡量我们重建的忠实度（match%）。它验证的是
  **真正运行的代码**与原 exe 的字节级差异。目标：每模块平均 match% ≥ 90%。
- 单个函数 ~90% 即收，不迭代死磕 100%（边际收益递减，时间花在未实现函数上更有价值）。
- **编译器**：MSVC 7.0（VS.NET 2002）via wine。
  `/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy`（**无 /G5 无 /Op 无 /GS**，与 th06 不同）。

## 2. 核心原则：诚实重建（纯 typed C++，对齐 th06 严格标准）

> **写标准 C++，让代码自己能跑。** objdiff 是验证工具，不是目标。
> th06 用纯 typed C++（零 raw address、零 raw[] buffer、零 accessor、
> 字符串字面量、浮点字面量、全部 struct 命名成员）达到 ~97% match——这证明
> 标准 C++ 本身就足够好，不需要任何地址作弊。
>
> **本节是项目宪法，由 `docs/TYPED_CPP_STANDARD.md` 细化执行。**
> 2026-06-18 已全项目达成此标准（见 PROGRESS.md P0.5 验收）。

### 【禁止】—— 一律改为 typed C++ 等价物

| 禁止形式 | 为什么禁止 | 正确做法 |
|---|---|---|
| **函数级 `#ifdef DIFFBUILD` 分裂** | objdiff 路径与运行路径函数体不同 | 单一代码路径 |
| **inline asm / `__declspec(naked)`** | asm 让验证与语义脱节；项目零 asm，保持 | 纯 C++ 表达式 |
| **DAT_ 常量槽**（`extern "C" u8 g_X[4]`）| 只为 reloc 作弊 | `extern "C" const f32` 或直接字面量 |
| **raw 绝对地址访问游戏状态**（`(*(T*)0x575aa8)`、`*reinterpret_cast<T*>(0xADDR)`）| 不可移植、跑不起来、非 th06 风格 | typed 成员访问 `g_Supervisor.curState` |
| **raw 绝对地址访问 rdata 字符串**（`(char*)0x496fe0`）| 同上 | 字符串字面量 `"bgm/thbgm.fmt"` |
| **raw 绝对地址作函数参数**（`(void*)0x4ba0d8`）| 同上 | typed 全局 `&g_SoundPlayer` |
| **raw 绝对地址调用函数**（`((void(*)())0x438668)()`）| 同上 | typed extern 声明 `Supervisor_Callback6()` |
| **`u8 raw[0xNNNN]` byte buffer**（整个 struct 当字节数组）| 绕过字段名、不可读、非 th06 风格（th06 Player 零 raw[]）| 全部命名成员；未知区域用 `u8 unk_XXXX[N]` 命名 padding |
| **accessor 方法返回 `&raw[OFF]`**（`T *Field() { return (T*)&raw[0x978]; }`）| 仍是 raw[] 伪装；th06 Player 零 accessor | 直接命名成员 `p->fieldName` |
| **`SCORE_SUB_I32(off)` 等 raw-offset 宏** | 同上 | typed struct 字段访问 `SCORE_SUB->counter14` |
| **对 typed global 做 `(T*)(bytes + idx*stride + off)` cast**（`*(i32*)(g_Nodes + idx*0xc + 0x4)`）| 把命名字节数组当 stride 算偏移，绕过字段名、magic number 满天飞 | 定义 element struct（`struct Node { i32 parent; i32 left; i32 right; };`）+ `g_Nodes[idx].parent`。**已验证**：MSVC 7.0 /Od 对两种写法生成字节级相同的 `IMUL reg,reg,stride` + `mov [reg+addr+off]`，objdiff match% 零变化（Pbg4Parser 95.40% 实测） |
| **`nullptr`** | MSVC 7.0 不支持 C++11 | `0` |
| **源码 UTF-8/CJK 字符**（含全角括号 `（）`）| MSVC 7.0 不认，且会致预处理器吞 struct 成员 | 注释必须 ASCII；Shift-JIS 字符串用八进制转义 |

**零例外**：D3D 设备、rdata 字符串、raw[] buffer、accessor、一切——全部走 typed C++。
`g_Supervisor.d3dDevice->Present()` 而非 `(*(IDirect3DDevice8**)0x575958)->Present()`；
`p->playerState` 而非 `p->raw[0x2408]` 或 `p->PlayerState()`。

### 【允许】—— 标准 C++ 工具

| 工具 | 用途 | 备注 |
|---|---|---|
| **typed C++ 全局/成员访问** | `g_Supervisor.curState`、`g_GameManager.difficulty` | **首选**，对齐 th06 |
| **命名 struct 成员** | `p->positionCenter`、`p->playerState`、`p->chainCalc` | th07 大 struct（如 Player 0xb7e78）的全部字段都命名；未知区域 `u8 unk_XXXX[N]` |
| **字符串字面量** | `"th07.cfg"`、`"data/text.anm"` | 接受 reloc 名差异（th06 也这么做）|
| **浮点/整数字面量** | `256.0f`、`ZUN_PI`、`0xff000000u` | 接受 `__real@` vs `DAT_` 差异 |
| **`extern "C" const f32` rdata 常量** | `extern "C" const f32 g_X = 1.0f;` + SYMBOL_MAP | 用于 orig 用 `fld [DAT_]` 形式的 rdata float |
| `DIFFABLE_STATIC/EXTERN` 宏 | 全局变量定义 | **仅影响变量定义**，不分裂函数代码。与 th06 一致 |
| `#pragma var_order` | 控制 MSVC /Od 局部栈布局 | 通过 `scripts/pragma_var_order.cpp` 注入 |
| `(T*)((u8*)typed_ptr + OFF)` 结构内部 offset | 仅当 OFF 落在已标注的 `unk_XXXX[]` padding 内 | typed base pointer + 显式 offset；诚实标注未验证区域。首选仍是提升为命名成员 |
| memset/memcpy intrinsics | 生成 `rep stosd`/`rep movsd` | 匹配 orig 批量初始化 |
| 去局部缓存 | 每次重读全局 | 匹配 orig 重读 idiom |
| early-return 控制流 | 每分支独立 return | 匹配 orig 错误路径 |
| `(u16)param` cast | 生成 `movzx` | 比 `& 0xFFFF` 精确 |

### th06 标杆对照（已验证——th06 的真实做法）

th06 达到 ~97% objdiff match，用的是：
- **纯 typed C++ 成员访问**：`g_Supervisor.d3dDevice->Present(0,0,0,0)`、
  `g_GameManager.arcadeRegionSize.x`、`g_SoundPlayer.PlaySoundByIdx(SOUND_1UP, 0)`——
  **零 raw address、零 raw[]、零 accessor**
- **全部 struct 命名成员**：th06 Player（0x98f0）全部 ~50 字段命名，
  含 `unk_XXXX` padding；**无 `u8 raw[]` buffer**
- **字符串字面量**：`g_AnmManager->LoadSurface(0, "data/title/th06logo.jpg")`——
  直接字面量，不 cast rdata 地址
- **浮点字面量**：`256.0f - effect->timer.AsFramesFloat() * 256.0f / 60.0f`——
  直接字面量，不 extern DAT_ 常量
- **`DIFFABLE_*` 宏**：只影响全局变量定义，不分裂函数代码 ✓

th07 已完全对齐（2026-06-18 全项目重构完成，见 PROGRESS.md P0.5）。

## 3. objdiff 与符号处理（对齐 th06）

objdiff 验证 typed C++ 编译出的 .obj 与 orig delinked .obj 的字节差异。
`scripts/generate_objdiff_objs.py` demangle 函数符号（与 th06 一致）。

`SYMBOL_MAP`（可选辅助）：少数情况下 typed global 的 mangled 名无法自然匹配 orig
DAT_ 符号时，可在 SYMBOL_MAP 加映射。但**这不是常规手段**——th06 无 SYMBOL_MAP 也达 97%。
首选是 typed C++ 自然匹配，SYMBOL_MAP 仅作兜底。

## 4. objdiff 工作流（验收）

1. **分析 orig**：ghidra-mcp `disassemble_function {address}`（权威）/ `decompile_function`（参考，注意误导：地址当值/+1漏标/逗号表达式/switch vs CMP链）
2. **写 C++**：参考 `src/Player.cpp`、`src/Supervisor.cpp`（typed C++ 范本）
3. **编译单模块**：`python3 scripts/build.py --build-type=objdiffbuild --object-name <Module>.obj`
4. **objdiff**：
   ```bash
   objdiff-cli diff -1 build/objdiff/orig/<M>.obj -2 build/objdiff/reimpl/<M>.obj \
     -o /tmp/<m>.json --format json-pretty
   ```
5. **match%** 在 `left.symbols[].match_percent`（snake_case）
6. **达标后**：`git commit` + 更新 `PROGRESS.md`

### 重新导出 orig obj（改 mapping.csv 后才需要，耗时 ~1min）
```bash
rm -rf /tmp/th07_new && mkdir -p /tmp/th07_new && \
/opt/ghidra/support/analyzeHeadless /tmp/th07_new TH07 \
  -import th07/th07.exe -scriptPath scripts/ghidra \
  -postscript ImportFromCsv.java config/mapping.csv \
  -postscript ExportDelinker.java config/ghidra_ns_to_obj.csv build/objdiff/orig -overwrite
```
**mkdir 必需**（analyzeHeadless 不建父目录）。mapping.csv 避免含空格/反引号的名字（如
`scalar deleting destructor`）和 UNKNOWN size，否则 ImportFromCsv 中断。

## 5. 工作目录与改动约束

- **仅限**项目目录 `/home/mystia/项目/TH07-RE` 和临时目录 `/tmp`。不触碰系统/家目录其他部分。
- 项目内删除/覆盖自主推进；系统目录/家目录改动先确认；方向性决策（选型/架构）先征询。

## 6. MSVC 7.0 硬限制

- 不支持 `nullptr`（C++11）→ 用 `0`（`ptr_field = 0` 编译成 `and [mem],0`，匹配 orig 零初始化 idiom）。
- 不认 UTF-8 中文注释（无 BOM 也无效）→ **源码注释必须英文**。
- callback `__fastcall`（th07 第四大差异，非 th06 的 `__cdecl`）。
- `extern "C"` 必须文件级（C2598），不能在函数体内。
- **禁止 inline asm / naked / DAT_ 常量槽 extern / 函数级 `#ifdef DIFFBUILD` 分裂 / raw 绝对地址**（见 §2）。

## 7. 当前状态（详见 PROGRESS.md）

- **objdiff**：22 模块跟踪，228 函数，per-module 算术平均 **79.39%**，**11 模块 ≥90%**
- **mapping.csv**：1562 行（th07.exe 全部非 thunk 函数）
- **typed-C++ 标准**：✅ 全项目达成（2026-06-18）。零 raw 绝对地址、零 `u8 raw[]`
  buffer、零 accessor。5 大 struct（Player/SoundPlayer/AsciiManager/Supervisor/
  GameManager）全部命名成员重构。
- **normal build**：全部 .cpp 编译通过；链接需 183 个跨模块符号实现（见 PROGRESS.md P0）
- **进行中**：P0 链接 / P1 低匹配模块（AnmManager ExecuteScript、BombData 11 calc、
  ReplayManager、Pbg4Parser）/ P2 抛光接近达标模块

## 8. 关键参考文件

| 文件 | 用途 |
|---|---|
| `PROGRESS.md` | 进度、每模块 match%、剩余工作（**必读**）|
| `docs/TYPED_CPP_STANDARD.md` | typed-C++ 风格宪法（§2 的执行细则、迁移 checklist、审计 greps）|
| `src/AnmManager.hpp` / `src/Player.hpp` | typed-C++ 全命名 struct 范本（含 scratchRegion 兜底范式）|
| `src/Player.cpp` / `src/Supervisor.cpp` | typed-C++ 函数体范本（零 raw address）|
| `src/diffbuild.hpp` | DIFFABLE_* 宏（全局变量定义，th06 继承）|
| `scripts/generate_objdiff_objs.py` | 函数符号 demangle + SYMBOL_MAP（typed global → orig DAT_ 映射）|
| `scripts/normalize_mapping.py` | mapping.csv merge/规范化工具（th06 格式）|
| `scripts/migrate_supervisor_cpp.py` / `migrate_gamemanager_cpp.py` | raw→typed 迁移脚本范本（可复用于新模块）|
| `scripts/pragma_var_order.cpp` | #pragma var_order 注入工具 |
| `config/mapping.csv` | ghidra 符号→地址映射（1562 行，th07.exe 全函数）|
| `objdiff.json` | objdiff 配置 |
