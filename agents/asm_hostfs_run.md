# Assembler MLI launch (implementation script)

**Transient.** Delete this file when the work is done. Do **not** commit
references to it from README, manual, or other `agents/` docs.

## Goal

Add Misc → Assembler **`[X] MLI launch`**: after a **successful** assemble,
optionally auto-run only if ProDOS MLI looks present at `$BF00`. The user’s
own asm **shim** (loaded into RAM by the assemble) does prefix + load/run via
real MLI. a2m does **not** invent ProDOS, RAM disks, boot shims, or HostFS
browsers.

Assemble **always** runs (including `file=` HostFS outputs). MLI launch only
gates the **post-success auto-run** chain.

## Non-goals (explicitly out of scope)

- Reboot / wait-for-boot / max-turbo boot / `boot_slot` orchestration
- Injecting `PRODOS` at `$2000` or calling OS “init”
- a2m-owned RAM disk or patched `BOOT.SYSTEM`
- HostFS-sandboxed path picker / prefix UI fields
- Live MLI call injection from the emulator (SET_PREFIX from C)
- Changing warm vs cold reset semantics globally
- Poking `$BF00` on reset to defeat false positives

## Locked product decisions

| # | Decision |
|---|----------|
| 1 | MLI check runs **after** successful assemble, **before** auto-run |
| 2 | No MLI → assemble still succeeds; **skip auto-run**; show a **notice** |
| 3 | Detection v1: CPU-visible byte at **`$BF00 == $4C`** (JMP) |
| 4 | If machine is running, **pause** before assemble; auto-run **resumes** (unchanged auto-run behavior) |
| 5 | **Reset machine** and **MLI launch** are mutually exclusive (both directions) |
| 6 | MLI launch UI enabled only when **Auto-run at** is on; turning Auto-run off clears MLI launch |
| 7 | Checkbox label: **`MLI launch`**; INI: `[assembler] mli_launch = yes\|no` (default **no**) |
| 8 | Ship a **sample shim** under `samples/` |
| 9 | Update **`manual/manual.md`** Assembler section |
| 10 | Minimum automated tests (see below) |

## Why Reset ⊥ MLI launch

Assembler **Reset machine** calls warm `apple2_reset()` (not Opt+F8 cold).

- Warm reset does **not** wipe language-card RAM; ProDOS bytes can remain.
- It **does** run `softswitch_setup_after_reset`, which clears **`A2S_LC_READ`**
  (LC write/bank2 armed, reads at `$D000–$FFFF` are **ROM**).
- So immediately after Assembler Reset, `$BF00` is typically **ROM**, not MLI,
  even if ProDOS still sits in LC RAM. Bitsy Bye “keeps working” after CTRL+RESET
  only if the machine **runs** through the reset vector and ProDOS remaps LC.
- Assembler Reset + immediate assemble + auto-run **skips** that recovery →
  shim `JSR $BF00` would hit ROM.

Therefore: enabling MLI launch forces Reset off; enabling Reset forces MLI
launch off. Do not document this as “Reset deletes ProDOS.”

## Current assemble path (baseline)

Worker (`runtime_assemble_file_command` in `src/runtime/runtime_thread.c`):

1. Optional `reset_first` → `apple2_reset()`
2. `runtime_assemble_file_ex_options(...)` (memory `dest=` + host `file=`)
3. On failure → `RUNTIME_EVENT_ASSEMBLE_ERROR`; stop
4. On success → if `auto_run`: set `PC = run_address`, `SP = $01FF`,
   `RUNTIME_EXEC_RUNNING`, publish running
5. Publish assemble complete (+ optional notice)

UI today: Auto-run at, Reset machine, Rearm one-shots (`src/frontend/frontend.c`
Assembler tab). Options/INI: `src/app_options.c` `[assembler]` keys.

## Desired post-success chain with MLI launch

```text
[optional: pause if running]
assemble (always)
if failed → error dialog; leave paused/stopped as today; stop
if MLI launch && auto_run:
    if mem[$BF00] != $4C:
        notice "MLI launch skipped: ProDOS MLI not present at $BF00"
        do NOT set PC / do NOT resume for auto-run
    else:
        auto-run as today (PC = run address, resume)
else:
    existing auto_run / no-auto_run behavior
```

Pause must happen **before** assemble when MLI launch is requested (and the
machine is running), so ProDOS is not executing while bytes are written.

## Implementation plan

### 1) Options / plumbing

Thread the flag end-to-end next to `auto_run` / `reset_first`:

- `app_options`: `assembler_mli_launch` (bool, default false)
- INI load/save: `[assembler] mli_launch`
- `frontend_assembler_options` / assembler dialog state
- `FRONTEND_DEBUGGER_INTENT_ASSEMBLE_RUN` fields
- `runtime_client_assemble_file_full(...)` + `RUNTIME_COMMAND_ASSEMBLE_FILE`
  payload (`mli_launch` uint8)

Defaults and copy/sync paths in `app_options.c` / `main.c` must include the new
field wherever `assembler_auto_run` / `assembler_reset_first` are handled.

### 2) Frontend (Assembler tab)

- Checkbox **`MLI launch`** under Auto-run (or immediately associated with it).
- Disabled (and forced off) when Auto-run is unchecked.
- Mutual exclusion with **Reset machine**:
  - Check MLI launch → clear Reset
  - Check Reset → clear MLI launch
- Persist via existing assembler options get/set.
- On Assemble / Shift+Opt+A: pass `mli_launch` in the intent.

Suggested UX copy (short): no need for a long help string in-panel; manual
covers semantics.

### 3) Runtime worker

In `runtime_assemble_file_command` (or a small helper):

1. If `mli_launch` and machine is running → transition to paused (same mechanism
   other commands use for pause; do not invent a new exec mode).
2. Honor `reset_first` only when set (UI should have cleared it when MLI launch
   is on; still safe if a stale command arrives: if both set, **prefer skipping
   reset when `mli_launch`** or treat as assert-level inconsistency — prefer
   **mli_launch wins: skip reset**).
3. Assemble as today.
4. On success, if `auto_run`:
   - If `mli_launch`: read one byte at `$BF00` via the **CPU-visible** map
     (`apple2_debug_read` / equivalent current read path — same bank the CPU
     would see for `JSR $BF00`).
   - If `!= 0x4C`: publish assemble-complete with **notice** (or a dedicated
     notice string on the existing complete event); **do not** auto-run.
   - If `== 0x4C` (or `mli_launch` false): existing auto-run (PC, SP, RUNNING).

Notice text (exact or close):

```text
MLI launch skipped: ProDOS MLI not present at $BF00
```

### 4) Sample shim (`samples/`)

Add a small, commented assembly example that:

- `.org $2000` (or documented run address)
- Assumes ProDOS MLI at `$BF00`
- Demonstrates **SET_PREFIX** (optional) + launch of a target
  - Prefer **QUIT** with a system pathname for `.SYSTEM` targets, **or**
    a short BIN open/read/JMP pattern — pick one primary path and document the
    other in comments
- Uses placeholder prefix / filename strings the user edits per project
- README blurb in that sample folder: requires live ProDOS; use Assembler
  Auto-run at `$2000` + **MLI launch**; leave Reset off; multi-file projects
  can `file=` outputs to HostFS and `dest="main"` for the shim in one assemble

Placement suggestion: `samples/asm_mli_launch/` (shim `.s` + short README).
Do not require HostFS mount for the sample to assemble; HostFS is the
intended real workflow.

### 5) Manual

Update `manual/manual.md` Assembler controls / INI table:

- Document **MLI launch**
- State: assemble always; gates auto-run only
- Detection: `$BF00 == $4C`
- Mutually exclusive with Reset; requires Auto-run
- Point at the sample shim path
- Do **not** mention this transient agents file

### 6) Tests (minimum bar)

Prefer a runtime-level test (pattern like `tests/runtime/test_runtime_*`):

| Case | Setup | Expect |
|------|--------|--------|
| A | `$BF00 = $4C`, `mli_launch` + `auto_run`, assemble tiny `org`/`dest` program | Assemble OK; PC == run address; running |
| B | `$BF00 ≠ $4C` (e.g. `$00`), same flags | Assemble OK; auto-run **not** applied (PC unchanged or not run address); notice path exercised if observable |
| C | Optional: `mli_launch` with `reset_first` also set in command | Reset skipped or MLI path still coherent (document choice in test name) |

Wire into ctest gate; bump expected count in `agents/testing.md` if that file
tracks the gate number (only if you normally update it when adding tests).

No obligation to UI-drive Nuklear for v1 if runtime coverage is solid.

## Files likely touched

- `src/app_options.h` / `.c`
- `src/frontend/frontend.h` / `.c`
- `src/runtime/runtime_command.h`
- `src/runtime/runtime_client.h` / `.c`
- `src/runtime/runtime_thread.c`
- `src/main.c` (intent → client; options sync)
- `manual/manual.md`
- `samples/asm_mli_launch/*` (new)
- `tests/runtime/...` + CMake test registration
- `agents/testing.md` (gate count only, if applicable)

## Acceptance checklist

- [ ] With ProDOS up (e.g. HostFS booted to Bitsy Bye), Auto-run + MLI launch,
      assemble shim → PC at run address, shim can call MLI
- [ ] Same with `$BF00` not `$4C` → assemble succeeds, no auto-run, notice shown
- [ ] MLI launch disabled/cleared when Auto-run off
- [ ] Checking MLI launch clears Reset; checking Reset clears MLI launch
- [ ] INI round-trip `mli_launch`
- [ ] Sample assembles under am65 or in-emulator assembler
- [ ] Manual updated
- [ ] Minimum ctests green
- [ ] Delete **this** file (`agents/asm_hostfs_run.md`) when done

## Rejected alternatives (context for implementers)

Do not reopen without product pushback:

- Reboot then assemble (wastes reboot on assemble failure; wrong order)
- Inject stock ProDOS + JMP `$2000` as the Assembler feature
- a2m RAM disk + configurable `BOOT.SYSTEM`
- Emulator-side MLI forge for prefix/launch
- Soft warning for arbitrary NAPS types (unrelated; already handled via Raw)

User-space shim + optional `$BF00` gate is the entire feature.
