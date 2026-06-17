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

## 2. 核心原则：诚实重建（单一代码路径）

> **objdiff 验证的就是运行的代码。** 绝不让 objdiff 路径与运行路径分离。

### 【禁止】—— objdiff 作弊形式（2026-06-17 polish 确立）

| 作弊形式 | 为什么是作弊 |
|---|---|
| **函数级 `#ifdef DIFFBUILD` 分裂** | objdiff 路径与运行路径函数体不同，验证的不是运行的 |
| **inline asm / `__declspec(naked)`** | 手写 asm 让 objdiff 看到的指令与 C++ 语义脱节；项目当前零 asm，保持 |
| **DAT_ 常量槽**（`extern "C" u8 g_Const[4]`）| 只为生成特定 reloc，无运行意义；改用 `extern "C" const f32` + SYMBOL_MAP |
| **`nullptr`** | MSVC 7.0 不支持 C++11；用 `0` |

### 【允许】—— 合法工具与技巧

| 工具 | 用途 | 备注 |
|---|---|---|
| `DIFFABLE_STATIC/EXTERN` 宏 | 全局变量定义 | **仅影响变量定义**（diffbuild 用 extern "C" 引用原 exe 全局、normal 用真实定义），**不分裂函数代码**。与 th06 一致 |
| `#pragma var_order` | 控制 MSVC /Od 局部变量栈布局 | 通过 `scripts/pragma_var_order.cpp` 的 naked shim 注入 |
| raw-offset field access | `*(T*)((u8*)this + OFF)` | 当 hpp struct 偏移与 orig 不符时绕过 |
| memset/memcpy intrinsics | 生成 `rep stosd`/`rep movsd` | 匹配 orig 的批量初始化 |
| 去局部缓存 | 每次重读全局而非缓存到 `[ebp-x]` | 匹配 orig 的重读 idiom |
| early-return 控制流 | 每分支独立 return | 匹配 orig 的 `OR EAX,-1`/`MOV EAX,-2` 错误路径 |
| `(u16)param` cast | 生成 `movzx` 而非 `mov+and` | 比 `& 0xFFFF` 更精确 |
| raw 绝对地址 | `(*(T*)0xADDR)` | **仅限** D3D 设备、rdata 字符串字面量等 orig 唯一形式单例；游戏状态对象（g_Supervisor 等）用 typed C++ global |

### th06 标杆对照（已验证）

th06 自己用 `DIFFABLE_*` 宏和 inline asm，但方式**诚实**：
- `DIFFABLE_*` 只影响**全局变量定义**，不分裂函数代码 ✓（th07 继承）
- inline asm（ZunMath.hpp fsincos）**无条件**，两种 build 都走 ✓
- th06 的 `#ifndef DIFFBUILD`（AnmManager.cpp）只包**数据表定义**，不是函数 ✓

th07 之前的错误：把 th06 的"数据表守卫"误用为"函数代码分裂"，把"无条件 asm"改成"守卫的 asm"。polish session 已纠正。

## 3. SYMBOL_MAP 恢复机制（关键）

typed C++ global 经 `scripts/generate_objdiff_objs.py` 的 `SYMBOL_MAP` 映射回 orig
DAT_ 地址，让 objdiff 正确比较，**无需代码分裂**。

```python
# scripts/generate_objdiff_objs.py
SYMBOL_MAP = {
    b"th07::g_Supervisor": b"DAT_00575950",
    b"th07::g_AnmManager": b"DAT_004b9e44",
    b"_g_EffectConst256": b"DAT_00498a98",  # extern "C" const f32，COFF 符号带前导 _
    # 新迁移的 global 按需添加
}
```

**迁移新 global 时**：声明为 typed C++ global（`extern Foo g_FooObj;`），在 owning
模块定义，然后在 SYMBOL_MAP 加 `demangled_name -> DAT_xxxxxxxx` 条目。objdiff 端
自动恢复匹配。

## 4. objdiff 工作流（验收）

1. **分析 orig**：ghidra-mcp `disassemble_function {address}`（权威）/ `decompile_function`（参考，注意误导：地址当值/+1漏标/逗号表达式/switch vs CMP链）
2. **写 C++**：参考 `src/Player.cpp`（最干净范本）、`src/Supervisor.cpp`（含 AddedCallback 正确逆向实例）
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
- **禁止 inline asm / naked / DAT_ 常量槽 extern / 函数级 `#ifdef DIFFBUILD` 分裂**（见 §2）。

## 7. 当前状态（详见 PROGRESS.md）

- **objdiff**：23 模块跟踪，228 函数，mean 75.81%，10 模块 ≥90%
- **normal build**：全部 .cpp 编译通过；链接需 183 个跨模块符号实现（见 PROGRESS.md P0）
- **最近里程碑**（2026-06-17 polish）：消除全部 objdiff 作弊代码（4 处 DIFFBUILD 分裂、5 处 inline asm、4 处 DAT_ extern），实现 Supervisor::AddedCallback（boot-critical）

## 8. 关键参考文件

| 文件 | 用途 |
|---|---|
| `PROGRESS.md` | 进度、每模块 match%、剩余工作（**必读**）|
| `src/Player.cpp` | 干净范本（stub struct / SYMBOL_MAP / 单一路径）|
| `src/Supervisor.cpp` | 干净范本（含 AddedCallback boot-critical 逆向实例）|
| `src/diffbuild.hpp` | DIFFABLE_* 宏（全局变量定义，th06 继承）|
| `scripts/generate_objdiff_objs.py` | SYMBOL_MAP 符号恢复 |
| `scripts/pragma_var_order.cpp` | #pragma var_order 注入工具 |
| `config/mapping.csv` | ghidra 符号→地址映射 |
| `objdiff.json` | objdiff 配置 |
