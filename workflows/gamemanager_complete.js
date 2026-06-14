export const meta = {
  name: 'gamemanager-complete',
  description: 'Deep-reverse GameManager remaining functions (file-based output): AddedCallback gate + ScoreSub contradiction + OnUpdate + DeletedCallback + IsGameActive/CalculateChecksum, then synthesize final struct',
  phases: [
    { title: 'Reverse', detail: '5 agents reverse functions, each writes complete draft to a markdown file' },
    { title: 'Synthesize', detail: 'merge into final GameManager.hpp struct + ScoreSub' },
  ],
}

const SUMMARY = {
  type: 'object',
  required: ['subject', 'filePath', 'wroteFile'],
  properties: {
    subject: { type: 'string' },
    filePath: { type: 'string', description: 'the file you wrote the draft to' },
    wroteFile: { type: 'boolean', description: 'true if you wrote the complete draft to filePath' },
    structFindings: { type: 'array', items: { type: 'string' }, description: 'key struct offsets resolved (e.g. "ScoreSub alloc=0xC8 confirmed", "+0x1fbac belongs to X")' },
    cppReady: { type: 'boolean', description: 'is the C++ in the file complete enough to compile, or just an outline?' },
    blockers: { type: 'array', items: { type: 'string' } },
    confidence: { type: 'string' },
  },
}

const SHARED = `PROJECT: th07.exe (Touhou 7) reconstruction. Rebuild C++ that compiles byte-exact under MSVC 7.0 (/MT /EHsc /Gs /DNDEBUG /Zi /Od /Oi /Ob1 /Gy; NO /G5/Op/GS) and validates via objdiff.

ESTABLISHED FACTS (do not re-derive):
- g_GameManager @ 0x626270 (sizeof 0x9700, NOT bigger — Item[1100] is in ItemManager @ 0x575c70, not here)
- g_Chain @ 0x626218 (AddToCalc/DrawChain are __thiscall, ECX=g_Chain)
- GameManager embeds ChainElem nodes: updateChainNode @ +0x9644, drawChainNode @ +0x9664
- GameManager +0x8 = ScoreSub* (DAT_00626278). ScoreSub +0x0=guiScore(u32), +0x4=score(u32). Allocation claimed 0xC8 but IsGameActive reads +0x1fbac — CONTRADICTION to resolve.
- GameManager +0x4 = playerSub (PlayerSub*, alloc 0x38)
- ScoreSub +0xac = crcAcc, +0xbc = crcStepBc (used by CalculateChecksum helper)
- chainActive/frameCounter @ +0x95e8. unk_93dc @ +0x93dc (OnDraw sets =2). statusBitfield @ +0x93d8.

CONVENTIONS (byte-match critical):
- MSVC 7.0 has NO nullptr — use literal 0. fn-ptr field = 0 emits 'and [mem],0'.
- callback assignment needs C-style cast: node.callback = (ChainCallback)OnUpdate;
- th07 callbacks are __fastcall (arg in ECX).
- #pragma var_order(a,b,c) controls stack layout.
- movzx trick: when orig loads an enum/int param as u16 via 'movzx word ptr [param]', use (u16)param NOT (param & 0xFFFF).
- objdiff LIMITATION: globals appear as '?g_GameManager@th07@@...(mangled)+offset' in reimpl vs 'DAT_xxxxxxxx' in orig (same address, different name) — global-heavy functions cap ~76%. Don't chase 100% on those; DO get logic + non-global instructions exact.

READ FIRST: /home/mystia/项目/TH07-RE/src/GameManager.hpp (current struct), src/GameManager.cpp (3 implemented funcs as style ref), workflows/gamemanager_synthesis.json (prior 38-field map).
ghidra-mcp is open (project TH07, program th07.exe). Use decompile_function / disassemble_function / get_xrefs_to / get_function_callees.

OUTPUT DISCIPLINE: Write your COMPLETE findings + C++ draft to the assigned file using the Write tool. The file is the deliverable. After writing, call StructuredOutput with a short summary (filePath + key points). Do NOT skip the Write.`

function agentPrompt(task) {
  return `${SHARED}

=== YOUR TASK: ${task.title} ===
${task.body}

ASSIGNED OUTPUT FILE: ${task.file}
Write your complete reversal + C++ implementation draft to that file (markdown with fenced cpp blocks). Be thorough — this file is what the human implements from. Include: (1) orig decompile/disasm key excerpts, (2) field-access table with offsets, (3) complete or near-complete C++ draft using th07 conventions, (4) any struct changes needed (ScoreSub/playerSub/GameManager fields with offsets), (5) dependencies on unreversed callees.
After Write, call StructuredOutput.`
}

const TASKS = [
  {
    id: 'addedcallback',
    file: '/home/mystia/项目/TH07-RE/workflows/draft_addedcallback.md',
    title: 'GameManager::AddedCallback @ 0x42e83e (2726B) — THE STRUCT-DEFINING INITIALIZER',
    body: `This is the gate function. Reverse it to lock the struct. Focus on:
1. ALL operator_new calls — confirm playerSub alloc size (0x38?) and ScoreSub alloc size (0xC8? VERIFY — IsGameActive reads ScoreSub+0x1fbac which exceeds 0xC8; find if a LARGER alloc happens here or the pointer is rebound elsewhere).
2. Every write to ScoreSub fields (offset + value/source) — build the complete ScoreSub layout.
3. Every write to playerSub fields.
4. Every write to GameManager header/control fields (+0x0..+0x96ec region).
5. The stage-load / RNG-seeding / anm-color-setup blocks — give their structure (not necessarily line-by-line, but the sequence of operations + field writes).
Produce a complete-as-possible C++ draft. If 2726B is too much for full line-by-line, prioritize: allocations, ALL field writes (offset+source), and the high-level flow; mark blocks you couldn't fully reverse as TODO. The field-write map is the critical deliverable.`,
  },
  {
    id: 'scoresub',
    file: '/home/mystia/项目/TH07-RE/workflows/draft_scoresub_resolution.md',
    title: 'RESOLVE the ScoreSub +0x1fbac contradiction (unblocks IsGameActive)',
    body: `IsGameActive (0x42ad66) reads *(GameManager+8)+0x1fbac but AddedCallback allocs only 0xC8 for it. Resolve:
1. Find ALL xrefs WRITING to 0x626278 (the scoreSub pointer slot, GameManager+8). Use get_xrefs_to 0x626278. Is the pointer rebound to a larger allocation after AddedCallback? By whom, what size?
2. Check if 0x1fbac is actually accessed relative to a DIFFERENT base (maybe IsGameActive's decompiler is misreading, or there's an intermediate pointer). Disassemble 0x42ad66 raw and trace the exact address computation.
3. Determine the TRUE nature of the value at GameManager+8 deref +0x1fbac: is it a game-state/activity flag? What reads/writes it?
4. Output: the resolution (pointer rebound? OOB read? different base?) + what struct/field +0x1fbac actually is, so IsGameActive can be implemented correctly.
This is a focused investigation — be rigorous with the xrefs.`,
  },
  {
    id: 'onupdate',
    file: '/home/mystia/项目/TH07-RE/workflows/draft_onupdate.md',
    title: 'GameManager::OnUpdate @ 0x42d8d5 (2303B) — per-frame update',
    body: `Reverse the per-frame update. Known structure (from prior scout): pause detection (input bit 8 of DAT_004b9e4c), rank adjustment (frame counters, DAT_00575ad8 thresholds, rankCounter@+0x962c mod 2/3/4/5/6 gating), stage-time/BGM fadeout (FadeOutMusic=FUN_0043a0d6 at frame counts 0x1fa4/0x1b6c/0x120c by difficultySelector@+0x93de), score smoothing (ScoreSub displayed->running via *(+0x8) pointer, step (target-displayed)>>5 clamped [1..0x8d55e], high-score update), out-of-bounds checks (many for-loops over *(param_1+8) substruct arrays — ranges 0x198f..0x1a02f), rank via DAT_009a9a80.
Produce a COMPLETE C++ draft (this is achievable — it's field ops + arithmetic, few calls). Get every threshold + bitfield op + loop exact. This function should be able to hit high objdiff% since it's mostly field arithmetic (less global-naming noise than RegisterChain).`,
  },
  {
    id: 'deletedcallback',
    file: '/home/mystia/项目/TH07-RE/workflows/draft_deletedcallback.md',
    title: 'GameManager::DeletedCallback @ 0x42f2e4 (~224B) — chain teardown',
    body: `Small function. Reverse fully + produce COMPLETE compilable C++. It shuts down MIDI (if Supervisor state matches — check MidiOutput calls), possibly frees scoreSub/playerSub (check free/operator delete), resets fields. Get the exact sequence + the MIDI condition. Should hit high objdiff%.`,
  },
  {
    id: 'small2',
    file: '/home/mystia/项目/TH07-RE/workflows/draft_isactive_checksum.md',
    title: 'GameManager::IsGameActive @ 0x42ad66 (69B) + CalculateChecksum @ 0x42d7be (~104B)',
    body: `Two small functions, COMPLETE C++ for both.
- IsGameActive: reads GameManager+8 (scoreSub*); if NULL return 0; else reads scoreSub+0x1fbac, return 0 if (<0 && !=-2) else 1. USE the ScoreSub resolution (draft_scoresub_resolution.md may not exist yet — make a reasonable assumption and note it). The +0x1fbac access is the open question; structure the C++ so the field access is clear.
- CalculateChecksum: anti-tamper. Calls helper FUN_0042d75a (byte-sum that also advances scoreSub->crcAcc@+0xac += crcStepBc@+0xbc per byte) 4 times: (a) scoreSub[0x34..0xac] len=(0xac-0x34)=0x78, (b) scoreSub[0xb4..0xc8] len=0x14, (c) playerSub[0..0x38] len=0x38, (d) global @ 0x575a68 len=0x38. Sum all 4 returns. The helper FUN_0042d75a is a separate function — declare it (static helper or extern) and call it 4x. Note: the CALL relocs will hit the objdiff naming limit; that's expected. Implement the helper too (it's small).
Produce complete C++ for both + the helper.`,
  },
]

phase('Reverse')
log(`Reversing ${TASKS.length} GameManager targets (file-based output)...`)
const results = await parallel(TASKS.map(t => () =>
  agent(agentPrompt(t), { label: t.id, phase: 'Reverse', schema: SUMMARY })
))
const valid = results.filter(Boolean)
const wroteFiles = valid.filter(r => r.wroteFile)
log(`${valid.length}/${TASKS.length} agents returned; ${wroteFiles.length} wrote files.`)

phase('Synthesize')
const synth = await agent(`${SHARED}

YOU ARE THE ARCHITECT. Read these draft files written by the reverse agents:
${TASKS.map(t => `- ${t.file}`).join('\n')}

Read each (they may or may not exist — skip missing ones). Then produce the FINAL GameManager.hpp struct + ScoreSub that reconciles everything:
1. FINAL ScoreSub: full field layout (offset+type+name) using AddedCallback's field writes + the +0x1fbac resolution. State the confirmed sizeof.
2. FINAL GameManager: confirm/update all offsets +0x0..+0x96ec.
3. Any new sub-struct (PlayerSub full if clarified).
4. Write the complete final struct to /home/mystia/项目/TH07-RE/workflows/gamemanager_final_struct.md (ready-to-paste C++ struct definitions).
5. List per-function implementation readiness + the key insight each needs.

Write the file, then call StructuredOutput.`,
  { label: 'synthesize-final', phase: 'Synthesize', schema: SUMMARY })

return { reverseSummaries: valid, synthesis: synth }
