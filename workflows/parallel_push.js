export const meta = {
  name: 'th07-parallel-push',
  description: 'Parallel reverse 6 remaining GameManager functions + analyze 2 Controller low-match diffs, synthesize updated struct',
  phases: [
    { title: 'Reverse', detail: '6 GameManager funcs (decompile+draft C++) + 2 Controller diff analyses, in parallel' },
    { title: 'Synthesize', detail: 'merge GameManager struct fields, resolve Item-array-base + ScoreSub contradictions' },
  ],
}

const SCHEMA = {
  type: 'object',
  required: ['subject', 'kind', 'findings'],
  properties: {
    subject: { type: 'string', description: 'function name or topic' },
    kind: { type: 'string', enum: ['gamemanager-func', 'controller-fix'] },
    decompileSummary: { type: 'string', description: 'what the function does, 2-5 sentences' },
    structFields: {
      type: 'array',
      description: 'NEW or CORRECTED GameManager/ScoreSub fields this function reveals (offsets not yet in the current hpp)',
      items: {
        type: 'object',
        properties: {
          baseStruct: { type: 'string', description: 'GameManager / ScoreSub / PlayerSub / Item / etc' },
          offset: { type: 'string' },
          type: { type: 'string' },
          proposedName: { type: 'string' },
          why: { type: 'string' },
        },
      },
    },
    cppDraft: { type: 'string', description: 'C++ implementation draft (for small funcs: complete; for big funcs: structured outline + key blocks + field accesses). Use th07 conventions.' },
    dependencies: { type: 'array', items: { type: 'string' }, description: 'unreversed callees / globals this needs' },
    diffAnalysis: { type: 'string', description: 'Controller only: what differs between orig and reimpl (per-instruction)' },
    proposedFix: { type: 'string', description: 'Controller only: minimal src/Controller.cpp change to raise match%' },
    contradictionsResolved: { type: 'string', description: 'did this resolve a known contradiction? (e.g. Item array base)' },
    confidence: { type: 'string' },
    notes: { type: 'string' },
  },
}

const SYNTH_SCHEMA = {
  type: 'object',
  required: ['updatedStruct', 'implementationPriority'],
  properties: {
    itemArrayResolution: { type: 'string', description: 'resolved Item array base offset + justification, or still-open' },
    scoreSubResolution: { type: 'string', description: 'resolved ScoreSub full layout / size, or still-open' },
    updatedStruct: { type: 'string', description: 'complete updated GameManager.hpp struct text (fields + opaque padding), ready to paste' },
    newSubStructs: { type: 'string', description: 'any new sub-struct definitions needed (ScoreSub full, Item, etc.)' },
    contradictionsRemaining: { type: 'array', items: { type: 'string' } },
    implementationPriority: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          function: { type: 'string' },
          difficulty: { type: 'string', description: 'trivial/easy/medium/hard/massive' },
          readyToImplement: { type: 'boolean' },
          blockers: { type: 'string' },
        },
      },
    },
  },
}

const SHARED = `PROJECT: th07.exe (Touhou 7) reverse-engineering reconstruction. Target: rebuild C++ source that compiles byte-exact (MSVC 7.0 + DX8, /Od /Oi /Ob1 /Gy, NO /G5/Op/GS) and validates via objdiff.

KEY FACTS:
- g_GameManager @ 0x626270 (the GameManager singleton; ~1.4MB struct embedding Item[1100]*0x288 + other entity arrays)
- g_Chain @ 0x626218 (global Chain controller; AddToCalcChain/AddToDrawChain are __thiscall, this/ECX = g_Chain)
- GameManager embeds its ChainElem nodes: updateChainNode @ +0x9644, drawChainNode @ +0x9664 (ChainElem is 0x20: +0x0 prio, +0x4 callback, +0x8 added, +0xc deleted, +0x10 prev, +0x14 next, +0x18 unkPtr, +0x1c arg)
- GameManager +0x8 = ScoreSub* (heap-allocated pointer). ScoreSub +0x0 = guiScore (u32), +0x4 = score (u32). IsGameActive reads ScoreSub +0x1fbac.
- chainActive flag @ GameManager +0x95e8. unk_93dc @ +0x93dc.

CONVENTIONS (critical for byte-match):
- MSVC 7.0 has NO nullptr — use literal 0. Assigning a function-pointer field to 0 emits 'and [mem],0' (matches orig zero-init idiom).
- callback assignment needs a C-style cast: node.callback = (ChainCallback)OnUpdate; (typedef is __fastcall *(*)(void*), methods take GameManager*).
- th07 callbacks are __fastcall (arg in ECX).
- #pragma var_order(local1, local2, ...) controls stack-frame layout to match orig.
- KNOWN objdiff LIMITATION: reimpl references globals as '?g_GameManager@th07@@...(mangled)+offset', orig delinks to 'DAT_xxxxxxxx' — SAME address, DIFFERENT name. objdiff compares operand strings, so functions with MANY global stores (RegisterChain) cap ~76% even when logically correct. Don't chase 100% on global-heavy functions; do chase logic correctness.

The ghidra-mcp instance is open (project TH07, program th07.exe). READ these first:
- /home/mystia/项目/TH07-RE/src/GameManager.hpp (current struct)
- /home/mystia/项目/TH07-RE/src/GameManager.cpp (3 already-implemented funcs as style reference)
- /home/mystia/项目/TH07-RE/workflows/gamemanager_synthesis.json (prior scout: 38-field table + substructs + 6 contradictions)
Use mcp__ghidra-mcp__decompile_function / disassemble_function / get_function_callees as needed.`

function gmAgent(f) {
  return () => agent(`${SHARED}

YOUR TASK: Fully reverse GameManager::${f.name} @ ${f.addr} (${f.size} bytes) and produce a C++ implementation draft.

${f.focus}

STEPS:
1. Decompile + disassemble the function via ghidra-mcp. Also decompile key callees if needed to understand them.
2. Extract EVERY GameManager/ScoreSub/Item field access with offset + type + semantics. Note fields NOT yet in the current GameManager.hpp (these are structFields in your output).
3. Produce a C++ draft. For a small function: complete, compilable implementation matching orig asm closely. For a big function: a structured outline (control flow + each block's logic + field accesses + the hard parts called out), since a full literal impl is too large for one pass — focus on getting the STRUCTURE and FIELD LAYOUT right.
4. List unreversed dependencies (callees/globals you couldn't fully pin down).
5. Note if you resolved a known contradiction (Item array base, ScoreSub size, etc.).

Return the structured object.`,
    { label: `gm:${f.name}`, phase: 'Reverse', schema: SCHEMA })
}

const GM_FUNCS = [
  { name: 'IsGameActive', addr: '0x42ad66', size: '0x44 (69B)', focus: 'Small leaf. Reads GameManager+0x8 (ScoreSub*); if NULL returns 0; else reads ScoreSub+0x1fbac, returns 0 if (<0 && !=-2) else 1. GOAL: determine what ScoreSub+0x1fbac is (this reveals the real size of ScoreSub — it must be >= 0x1fbb0, contradicting the earlier operator_new(0xC8) claim). Check what writes ScoreSub+0x1fbac (xref) to name it. Produce complete C++.' },
  { name: 'CalculateChecksum', addr: '0x42d7be', size: '~0x68', focus: 'Anti-tamper: calls FUN_0042d75a 4 times (first with (param_1,param_1), rest no visible args) and sums results. REVERSE FUN_0042d75a (it checksums a memory region). Determine the 4 regions. Produce complete C++. This is an anti-cheat — getting it byte-exact matters for the self-checks of the game.' },
  { name: 'DeletedCallback', addr: '0x42f2e4', size: '~0xe0 (224B)', focus: 'Chain teardown. Shuts down MIDI (if Supervisor state matches), possibly frees ScoreSub/playerSub. Decompile + identify the MIDI calls and any frees. Produce complete C++.' },
  { name: 'OnUpdate', addr: '0x42d8d5', size: '0x8fe (2303B)', focus: 'Per-frame update: pause detection (input bit 8), rank adjustment (frame counters, DAT_00575ad8 thresholds), stage-time/BGM fadeout (FUN_0043a0d6=FadeOutMusic at frame counts 0x1fa4/0x1b6c/0x120c by difficultySelector@+0x93de), score smoothing (ScoreSub displayed->running via *(+0x8) pointer, clamp 999999999), out-of-bounds checks (many for-loops over *(param_1+8) substruct arrays), rank via DAT_009a9a80 (bullet count?). BIG — produce structured outline + field map. The score-smoothing block and the for-loop bounds-check blocks are the bulk.' },
  { name: 'AddedCallback', addr: '0x42e83e', size: '0xaa5 (2726B)', focus: 'Stage-load initializer. Allocates playerSub (operator_new 0x38) and scoreSub (operator_new — VERIFY THE SIZE: IsGameActive reads +0x1fbac so scoreSub >= 0x1fbb0, NOT 0xC8). Loads stage data, seeds RNG into ScoreSub fields, sets up anm/colors. BIG — produce structured outline + the allocation sizes + which ScoreSub fields get seeded. Resolving the true allocation size of scoreSub is a KEY deliverable.' },
  { name: 'OnItemUpdate', addr: '0x432990', size: '0x10c9 (4298B)', focus: 'THE Item dispatcher — walks the Item[1100] array. CRITICAL DELIVERABLE: resolve the Item array base offset (current hpp guesses +0xae2f0 but activeItemListHead@+0xae574 falls INSIDE it — contradiction). Disassemble the loop setup: what pointer does it iterate (param_1+OFFSET with stride 0x288)? That OFFSET is the true Item base. Also map the 10-way switch (item types) and the Item struct layout (0x288 bytes — reverse the field accesses). This is the biggest function; focus on (a) Item array base resolution, (b) Item struct fields, (c) switch dispatch structure. A full C++ impl is out of scope — give outline + field map + the resolved base.' },
]

function ctrlAgent(f) {
  return () => agent(`${SHARED}

YOUR TASK: Analyze why Controller::${f.name} scores only 83.8% in objdiff and propose a MINIMAL source fix.

CONTEXT: Controller is validated (7 funcs, avg 93.3%). Two SetButtonFrom* funcs sit at 83.8%.
- orig obj: /home/mystia/项目/TH07-RE/build/objdiff/orig/Controller.obj
- reimpl obj: /home/mystia/项目/TH07-RE/build/objdiff/reimpl/Controller.obj
- objdiff json already generated at /tmp/controller.json (read it; match_percent field is snake_case).
- reimpl source: /home/mystia/项目/TH07-RE/src/Controller.cpp (functions ${f.name} around line ${f.line})
- orig function @ ${f.addr} (decompile via ghidra-mcp to see exact orig asm).

STEPS:
1. From /tmp/controller.json, extract the per-instruction comparison for ${f.name} (left vs right instructions). objdiff pairs them; find which instructions differ.
2. Decompile/disassemble orig @ ${f.addr} via ghidra-mcp. Compare to the reimpl source.
3. Identify the ROOT CAUSE of the 83.8% (likely: a calling-convention epilogue 'ret N', a return-value width issue u16 vs u32, an extra MOV, or a logic micro-difference). Be specific about WHICH instructions differ.
4. Propose a MINIMAL src/Controller.cpp change (in proposedFix) that should raise match%. Consider MSVC 7.0 codegen quirks (e.g. return type width, parameter handling, the redundant-store idiom).

Do NOT write files. Output the structured object with diffAnalysis + proposedFix.`,
    { label: `ctrl:${f.name}`, phase: 'Reverse', schema: SCHEMA })
}

const CTRL_FUNCS = [
  { name: 'SetButtonFromDirectInputJoystate', addr: '0x4302f0', line: 229 },
  { name: 'SetButtonFromControllerInputs', addr: '0x430370', line: 242 },
]

phase('Reverse')
log(`Reversing ${GM_FUNCS.length} GameManager funcs + ${CTRL_FUNCS.length} Controller diffs in parallel...`)
const results = await parallel([
  ...GM_FUNCS.map(gmAgent),
  ...CTRL_FUNCS.map(ctrlAgent),
])
const valid = results.filter(Boolean)
const gmResults = valid.filter(r => r.kind === 'gamemanager-func')
const ctrlResults = valid.filter(r => r.kind === 'controller-fix')
log(`Got ${gmResults.length} GameManager + ${ctrlResults.length} Controller analyses. Synthesizing struct...`)

phase('Synthesize')
const synth = await agent(`${SHARED}

YOU ARE THE ARCHITECT. Below are ${gmResults.length} structured reversals of GameManager functions (JSON). Merge them into an updated GameManager.hpp struct and resolve the open contradictions.

GAMEMANAGER ANALYSES (JSON):
${JSON.stringify(gmResults, null, 2)}

TASKS:
1. ITEM ARRAY BASE (critical): reconcile the +0xae2f0 guess vs activeItemListHead@+0xae574 contradiction using OnItemUpdate's loop setup. State the resolved base + the active-item list head/tail offsets + final sizeof(GameManager) estimate.
2. SCORESUB SIZE: reconcile the operator_new(0xC8) claim vs IsGameActive's +0x1fbac access. State ScoreSub's true size + key fields.
3. UPDATED STRUCT: emit the COMPLETE updated GameManager.hpp struct text (all confirmed fields at correct offsets, opaque u8[] padding for gaps, ChainElem nodes at +0x9644/+0x9664, Item array at resolved base, tail to final sizeof). Mark provisional regions. Ready to paste.
4. NEW SUBSTRUCTS: emit any new struct defs needed (full ScoreSub, Item if reversed, PlayerSub if clarified).
5. CONTRADICTIONS REMAINING: list anything still unresolved.
6. IMPLEMENTATION PRIORITY: rank the 6 functions by difficulty + ready-to-implement (blockers noted). This guides what the human implements next.

Output the structured object. The updatedStruct will become the new GameManager.hpp.`,
  { label: 'synthesize-struct-v2', phase: 'Synthesize', schema: SYNTH_SCHEMA })

return { gamemanager: gmResults, controller: ctrlResults, synthesis: synth }
