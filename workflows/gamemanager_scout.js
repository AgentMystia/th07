export const meta = {
  name: 'gamemanager-scout',
  description: 'Scout th07 GameManager module: parallel decompile+analyze 9 core/helper functions, synthesize struct layout + method table',
  phases: [
    { title: 'Analyze', detail: 'decompile each GameManager function via ghidra-mcp, extract struct field accesses' },
    { title: 'Synthesize', detail: 'merge field accesses into GameManager struct layout + method table + dependencies' },
  ],
}

// GameManager core methods + highest-frequency helpers (by xref count to g_GameManager)
const FUNCS = [
  { addr: '0x42f3c5', role: 'GameManager::RegisterChain (chain registration)' },
  { addr: '0x42f45d', role: 'GameManager::CutChain (chain teardown + score clamp)' },
  { addr: '0x42d8d5', role: 'GameManager::OnUpdate (per-frame: pause/rank/stage-time/score-smooth, ~6398 bytes)' },
  { addr: '0x42e1d4', role: 'GameManager::OnDraw' },
  { addr: '0x42e83e', role: 'GameManager::AddedCallback (initialization)' },
  { addr: '0x42f2e4', role: 'GameManager::DeletedCallback' },
  { addr: '0x432990', role: 'HIGH-FREQ helper (20+ xrefs to g_GameManager) — likely Item/Enemy/score dispatcher' },
  { addr: '0x42d7be', role: 'OnUpdate callee (time/rank helper)' },
  { addr: '0x42ad66', role: 'OnUpdate callee' },
]

const SCHEMA = {
  type: 'object',
  required: ['functionAddr', 'inferredName', 'semantics', 'fields'],
  properties: {
    functionAddr: { type: 'string' },
    inferredName: { type: 'string', description: 'e.g. GameManager::OnUpdate' },
    callingConvention: { type: 'string', description: '__fastcall / __thiscall / __cdecl' },
    hasReturn: { type: 'string', description: 'return type inferred' },
    semantics: { type: 'string', description: 'what the function does, 2-5 sentences' },
    fields: {
      type: 'array',
      description: 'every field access on the GameManager struct',
      items: {
        type: 'object',
        required: ['offset', 'type', 'semantics'],
        properties: {
          offset: { type: 'string', description: 'hex offset from GameManager base (0x626270). Use "*(+0x8)+0x4" for dereferenced-pointer sub-struct fields.' },
          type: { type: 'string', description: 'i32/f32/u32/u16/u8/i8/ptr/bool/array[N elem]' },
          semantics: { type: 'string' },
          accessMode: { type: 'string', description: 'read/write/readwrite' },
        },
      },
    },
    calledFunctions: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          addr: { type: 'string' },
          purpose: { type: 'string' },
        },
      },
    },
    keyLogicNotes: { type: 'string', description: 'non-obvious logic (thresholds/state-transitions/bitfields/inline loops) that must be preserved for exact reimplementation' },
    reimHints: { type: 'string', description: 'hints for C++ reimplementation: inlining decisions, #pragma needs, stack-frame quirks' },
  },
}

function promptFor(f) {
  return `You are assisting the TH07.exe (Touhou 7) reverse-engineering reconstruction project. Target: rebuild th07.exe from C++ source compiled with MSVC 7.0 + DX8, verified byte-exact via objdiff. Use the ghidra-mcp tools (the instance is already open: project TH07, program th07.exe).

Call the tool \`mcp__ghidra-mcp__decompile_function\` with address "${f.addr}". Also use \`mcp__ghidra-mcp__get_function_callees\` (address "${f.addr}") and \`mcp__ghidra-mcp__get_function_by_address\` (address "${f.addr}") to get the body range and callees. If you need to understand a callee, decompile it too.

CONTEXT — GameManager module:
- Global GameManager instance g_GameManager is at address 0x626270.
- Chain callbacks (OnUpdate/OnDraw/AddedCallback/DeletedCallback) receive param_1 = GameManager* (== &g_GameManager). So param_1 == 0x626270.
- Known GameManager methods: RegisterChain=0x42f3c5, CutChain=0x42f45d, OnUpdate=0x42d8d5, OnDraw=0x42e1d4, AddedCallback=0x42e83e, DeletedCallback=0x42f2e4.
- th07 callbacks use __fastcall (ECX = first arg).
- The struct HEADER differs from the th06 reference (th06: guiScore@0x0, score@0x4, nextScoreIncrement@0x8, highScore@0xc). In th07 the layout is DIFFERENT: offset +0x8 (DAT_00626278) appears to be a POINTER to a score sub-struct (OnUpdate does \`*(param_1+8)\` then +4/+8/+0xc). Offset +0x4 (DAT_00626274) is accessed like \`*(g_GameManager+4 + 0x25)\` suggesting +0x4 may itself be a pointer or the start of a sub-object.
- This is the LARGEST module in th07 (~0x9700+ byte struct suspected; embeds Item array 1100 entries x 0x288 bytes per earlier analysis). Treat large contiguous offsets carefully.

YOUR FUNCTION TO ANALYZE: ${f.addr} — likely role: ${f.role}.

TASKS:
1. Decompile it. Get its body range (start-end) to know its true size.
2. Extract EVERY field access on the GameManager struct. Two forms to capture:
   (a) param_1+offset  (param_1 is the GameManager* argument)
   (b) DAT_00626270+offset  /  DAT_00626274 (=base+4) / DAT_00626278 (=base+8) / DAT_0062627d (=base+0xd) etc. — ANY DAT_006262xx / DAT_00626xxx within the struct range is a GameManager field.
   For each: hex offset, inferred C type (i32/f32/u32/u16/u8/i8/ptr/bool/array), semantics (best guess from context), read/write.
3. If param_1+8 (or +4) is a POINTER to a sub-struct, record sub-struct fields as "*(+0x8)+0xN" with their own type/semantics.
4. Describe the function semantics in 2-5 sentences.
5. List called sub-functions (addr + inferred purpose). For unknown callees, guess the module (AnmManager/Supervisor/SoundPlayer/Chain/CRT).
6. Note non-obvious logic: magic thresholds (e.g. 0x1fa4, 999999999, 0x8d55e), state-machine transitions, bitfield ops (>> N & 1), inline loops over arrays — these MUST be preserved for an exact reimplementation.
7. Reimplementation hints: inlining decisions, any #pragma auto_inline needs, stack-frame quirks, calling-convention traps.

Be PRECISE about offsets — they will be merged into the canonical GameManager.hpp struct definition. When unsure of a type, give your best guess and mark it. Return the structured object.`
}

const SYNTH_SCHEMA = {
  type: 'object',
  required: ['structFields', 'structSizeGuess', 'methods', 'dependencies'],
  properties: {
    structFields: {
      type: 'array',
      description: 'complete field table offset 0x0..max, sorted ascending',
      items: {
        type: 'object',
        required: ['offset', 'type', 'semantics'],
        properties: {
          offset: { type: 'string' },
          name: { type: 'string', description: 'proposed C field name' },
          type: { type: 'string' },
          semantics: { type: 'string' },
          isArray: { type: 'boolean' },
          arrayInfo: { type: 'string', description: 'e.g. "1100 x 0x288 = 0xB3020 bytes" if determinable' },
          confidence: { type: 'string', description: 'high/medium/low' },
        },
      },
    },
    structSizeGuess: { type: 'string', description: 'hex, e.g. 0x97xx — justify from max offset' },
    methods: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          addr: { type: 'string' },
          name: { type: 'string' },
          callingConvention: { type: 'string' },
          size: { type: 'string' },
          semantics: { type: 'string' },
        },
      },
    },
    dependencies: {
      type: 'array',
      items: {
        type: 'object',
        properties: { addr: { type: 'string' }, module: { type: 'string' }, purpose: { type: 'string' } },
      },
    },
    subStructs: {
      type: 'array',
      description: 'nested structs reached via pointer fields (e.g. +0x8 score sub-struct)',
      items: {
        type: 'object',
        properties: {
          reachedVia: { type: 'string', description: 'e.g. "+0x8 pointer"' },
          fields: { type: 'array', items: { type: 'object', properties: { offset: { type: 'string' }, type: { type: 'string' }, semantics: { type: 'string' } } } },
        },
      },
    },
    contradictions: { type: 'array', items: { type: 'string' }, description: 'same offset typed/used differently across functions — needs human resolution' },
    recommendedNextSteps: { type: 'array', items: { type: 'string' }, description: 'what to reverse next to complete GameManager' },
  },
}

function synthPrompt(analyses) {
  return `You are the architect of the TH07 (Touhou 7) reconstruction project. Below are structured analyses (JSON array) of 9 th07 GameManager core + helper functions, produced by parallel reverse-engineering agents using Ghidra. Your job: merge them into a complete th07::GameManager struct layout and a method table.

ANALYSES (JSON):
${JSON.stringify(analyses, null, 2)}

TASKS:
1. STRUCT FIELDS: Produce a complete field table for th07::GameManager from offset 0x0 up to the maximum observed offset, sorted ascending. Merge duplicate offsets across functions (prefer the most specific/sensible type and semantics). Mark arrays where you see contiguous same-type accesses with a stride. Note pointer fields — especially offset +0x8 (likely a pointer to a score sub-struct) and +0x4. For each field give a proposed C name, type, semantics, and confidence (high/medium/low).
2. SUBSTRUCTS: For pointer fields (e.g. +0x8), describe the sub-struct they reach (its fields) separately.
3. STRUCT SIZE: Infer a ZUN_ASSERT_SIZE guess (max offset + last field size). Justify. Note the giant embedded arrays (Item 1100x0x288 etc.) if the offsets support it.
4. METHODS: List all GameManager methods found (addr + inferred C++ name + calling convention + size + one-line semantics).
5. DEPENDENCIES: List external-module functions the GameManager calls (AnmManager, Supervisor, SoundPlayer, Chain, CRT/memset, etc.) with addresses and purposes.
6. CONTRADICTIONS: Flag any offset typed/used differently across functions so a human can resolve them (e.g. +0x8 read as pointer in one place, as u32 in another).
7. NEXT STEPS: Recommend what to reverse-engineer next to complete GameManager (e.g. "FUN_00432990 is the Item dispatcher — reverse it to map the Item array region").

Output the structured object. The structFields table will become GameManager.hpp.`
}

phase('Analyze')
log(`Analyzing ${FUNCS.length} GameManager functions in parallel...`)
const analyses = await parallel(FUNCS.map(f => () =>
  agent(promptFor(f), { label: `analyze:${f.addr}`, phase: 'Analyze', schema: SCHEMA })
))
const valid = analyses.filter(Boolean)
log(`Got ${valid.length}/${FUNCS.length} analyses. Synthesizing struct layout...`)

phase('Synthesize')
const synth = await agent(synthPrompt(valid), { label: 'synthesize-gamemanager-struct', phase: 'Synthesize', schema: SYNTH_SCHEMA })

return { functionCount: valid.length, analyses: valid, synthesis: synth }
