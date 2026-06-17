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

## 2. 核心原则：诚实重建（纯 typed C++，对齐 th06 标准）

> **写标准 C++，让代码自己能跑。** objdiff 是验证工具，不是目标。
> th06 用纯 typed C++（零 raw address、字符串字面量、浮点字面量）达到 ~97% match——
> 这证明标准 C++ 本身就足够好，不需要任何地址作弊。

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
| **`nullptr`** | MSVC 7.0 不支持 C++11 | `0` |

**零例外**：D3D 设备、rdata 字符串、一切——全部走 typed C++。
`g_Supervisor.d3dDevice->Present()` 而非 `(*(IDirect3DDevice8**)0x575958)->Present()`。

### 【允许】—— 标准 C++ 工具

| 工具 | 用途 | 备注 |
|---|---|---|
| **typed C++ 全局/成员访问** | `g_Supervisor.curState`、`g_GameManager.arcadeRegionSize.x` | **首选**，对齐 th06 |
| **字符串字面量** | `"th07.cfg"`、`"data/text.anm"` | 接受 reloc 名差异（th06 也这么做）|
| **浮点/整数字面量** | `256.0f`、`ZUN_PI`、`0xff000000u` | 接受 `__real@` vs `DAT_` 差异 |
| `DIFFABLE_STATIC/EXTERN` 宏 | 全局变量定义 | **仅影响变量定义**，不分裂函数代码。与 th06 一致 |
| `#pragma var_order` | 控制 MSVC /Od 局部栈布局 | 通过 `scripts/pragma_var_order.cpp` 注入 |
| raw-offset field access | `*(T*)((u8*)this + OFF)` | 仅当 struct 字段类型不匹配且无法改 hpp 时；首选改 hpp |
| memset/memcpy intrinsics | 生成 `rep stosd`/`rep movsd` | 匹配 orig 批量初始化 |
| 去局部缓存 | 每次重读全局 | 匹配 orig 重读 idiom |
| early-return 控制流 | 每分支独立 return | 匹配 orig 错误路径 |
| `(u16)param` cast | 生成 `movzx` | 比 `& 0xFFFF` 精确 |

### th06 标杆对照（已验证——th06 的真实做法）

th06 达到 ~97% objdiff match，用的是：
- **纯 typed C++ 成员访问**：`g_Supervisor.d3dDevice->Present(0,0,0,0)`、
  `g_GameManager.arcadeRegionSize.x`、`g_SoundPlayer.PlaySoundByIdx(SOUND_1UP, 0)`——
  **零 raw address**
- **字符串字面量**：`g_AnmManager->LoadSurface(0, "data/title/th06logo.jpg")`——
  直接字面量，不 cast rdata 地址
- **浮点字面量**：`256.0f - effect->timer.AsFramesFloat() * 256.0f / 60.0f`——
  直接字面量，不 extern DAT_ 常量
- **`DIFFABLE_*` 宏**：只影响全局变量定义，不分裂函数代码 ✓
- **无 SYMBOL_MAP**：th06 的 `generate_objdiff_objs.py` 只 demangle 函数符号，
  不重命名跨模块引用——typed C++ 自然匹配

th07 应完全对齐：typed C++ + 字面量。SYMBOL_MAP 不是常规手段。

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

- **objdiff**：23 模块跟踪，228 函数，mean 75.81%，10 模块 ≥90%
- **normal build**：全部 .cpp 编译通过；链接需 183 个跨模块符号实现（见 PROGRESS.md P0）
- **进行中**：raw address 全量迁移到 typed C++（对齐 th06 标准；692 处 → 目标零）

## 8. 关键参考文件

| 文件 | 用途 |
|---|---|
| `PROGRESS.md` | 进度、每模块 match%、剩余工作（**必读**）|
| `src/Player.cpp` / `src/Supervisor.cpp` | typed C++ 范本 |
| `src/diffbuild.hpp` | DIFFABLE_* 宏（全局变量定义，th06 继承）|
| `scripts/generate_objdiff_objs.py` | 函数符号 demangle + 可选 SYMBOL_MAP |
| `scripts/pragma_var_order.cpp` | #pragma var_order 注入工具 |
| `config/mapping.csv` | ghidra 符号→地址映射 |
| `objdiff.json` | objdiff 配置 |
