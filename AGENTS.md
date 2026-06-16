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

## 终止条件（遇硬阻塞才停）
- 需重新导出 orig obj 且非自主可控
- 结构体偏移矛盾无法仅靠项目内修改解决
- 长时间无有效进展
