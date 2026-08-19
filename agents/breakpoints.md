# Debugger breakpoints + bus observability

**Epic status:** product breakpoints (H7) — **0–3 + P4a–P4e done**.  
**Open:** P5 control wire and P4b TRON (both ride the remote-debug epic).  
**Architecture north star:** keep the a2m page-map memory model; restore c64m
**runtime/debugger** breakpoint quality; enhance Apple bus observability only
where tools need it (R/W watchpoints, later history).

Related: [`status.md`](status.md) · [`runtime.md`](runtime.md) ·
[`remote-debug.md`](remote-debug.md) · [`rules.md`](rules.md). c64m gold for worker
helpers: git `c4d79b8` `src/runtime/runtime_thread.c` (~2180–3100, command cases
~5180+).

---

## Decision (product / architecture)

| Question | Answer |
|----------|--------|
| Work first: BP product path or enhanced bus model? | **Breakpoints first.** Exec BPs need no bus hook. |
| Replace a2m page maps with c64m bus architecture? | **No.** Wrong machine cost; a2m model is sufficient for Apple banking. |
| What to copy from c64m? | Debugger UX, command surface, match/counter/condition **runtime** engine. |
| What not to copy? | VIC-centric terms as product center, 1541 metaphors, full multi-chip bus graph. |

### Memory model (sufficient vs enhance)

| Layer | Status | Notes |
|-------|--------|-------|
| Softswitch + `read_pages` / `write_pages` | **Keep** | a2m industrial sketch; banking truth |
| `apple2_bus_read` / `apple2_bus_write` | **Keep + later hook** | Single choke point for tools |
| Live R/W access callback | **Missing** | Needed only for Read/Write BPs (Phase 3) |
| Per-addr write_history | **Missing** | Debugger annotation; not BP match |
| Flight-recorder ring | **Active under remote-debug C3** | Separate timeline; hang off same choke — [`remote-debug.md`](remote-debug.md) |

Three c64m mechanisms (do not collapse):

1. **`memory_access` callback** — live R/W **breakpoint** probe (not a ring).  
2. **`write_history[addr]`** — last writer PC pack for mem view.  
3. **CPU history observer** — instruction timeline / remote history.

Phase 3 is (1) only.

---

## Transfer table (c64m → a2m)

| Piece | Transfers? | Notes |
|-------|------------|--------|
| Data model (definition, snapshot, access/action/mapping) | **Yes — already in tree** | `runtime_event.h`, `runtime_internal.h` |
| Client create/update/enable/duplicate/rearm/clear/request | **Yes — client OK** | Worker was thin |
| Misc Breakpoints UI + dialog + intents | **Yes — already** | Apply was no-op until CREATE lands |
| Disasm toggle SET_EXECUTE | **Fix host intent** | Intent used `.value`; main read `.address` |
| Worker CREATE/UPDATE/DUPLICATE/SET_ENABLED/REARM | **Restore from c64m** | Was `default: ignore` after W2 |
| Full snapshot publish (hits, counters, paths, condition) | **Restore** | List labels need fields |
| Exec match: range wrap, counter, condition, BREAK | **Restore / adapt** | Pure runtime + Apple CPU/map read |
| Composite Apple mapping | **Done (P2)** | Shared `VIEW_FLAGS`: RAM Map/Main/Aux, C100 Map/ROM, D000 Map/LC1/LC2/ROM |
| Condition A/X/Y/SP/P/flags/value/mem | **Yes** | Fill eval context from `apple2_t` |
| Condition `raster` / `vic_cycle` | **Adapt** | Map to Apple beam `line` / `cycle_in_line` |
| READ/WRITE access match | **Needs bus hook** | Phase 3 |
| Actions FAST/SLOW | **Done (P4a)** | FAST→max, SLOW→1 MHz (zip policy) |
| Actions TRON/TROFF/TYPE/SWAP | **TYPE + SWAP done**; TRON deferred | Trace file later; TYPE script; Disk II multi-image SWAP |
| INI `[DEBUG] break.*` | **Done (P4e)** | Load at worker start; save on quit with `--saveini`/`--remember` |
| Control-port BP RPC | **Open — remote-debug C1 (P5)** | Engine ready; wire with A2M product control |
| SP `$C800` host trap addrs as exec BPs | **Document** | Will not fire as 6502 ops |

---

## Phases

### Phase 0 — Product create path (unblocks Misc Apply)

- CREATE / UPDATE / DUPLICATE / SET_ENABLED / REARM command handlers  
- Shared `apply_definition` / `add_breakpoint` (ini loader can call same shape)  
- Full snapshot publish  
- SET_EXECUTE via full definition path (toggle existing simple exec BP)  
- Fix SET_EXECUTE intent address/value  
- ctest: create + list + free-run hit  

**Exit:** Misc New → Apply shows in list; disasm toggle + `--break` arm correct address.

### Phase 1 — Exec match pipeline (c64m parity for execute)

- Address range (+ wrap)  
- Counter / oneshot / rearm  
- Condition eval (regs, mem via `apple2_debug_read`; beam as raster/cycle)  
- Reject exec + `value` condition at create  
- Mapping Map always; explicit views use the shared Apple `VIEW_FLAGS` model
- Actions: **BREAK** fully; FAST/SLOW → turbo; others no-op until Phase 4  

**Exit:** Range/counter/guarded exec BPs behave like c64m for 6502.

### Phase 2 — Mapping polish (Apple planes)

- Product labels are three independent rows: **RAM Map/Main/Aux**, **C100 Map/ROM**, **D000 Map/LC1/LC2/ROM**.
- Match uses the active read or write page map according to breakpoint access and shares the Memory window's `VIEW_FLAGS` representation.
- INI tokens: `map`, `main`, `aux`, `c100map`, `c100rom`, `d000map`, `lc1`, `lc2`, `rom` (`ram` accepted as legacy → map).

### Phase 3 — Enhanced model (R/W watchpoints)

- Optional callback on `apple2_bus_read` / `apple2_bus_write`  
- Prefer call only when any R/W BP armed (`has_rw_breakpoints`)  
- `breakpoint_hit_pending` + pause after access (c64m pattern)  
- Wire `matches_access` for READ/WRITE  

**Exit:** Misc Read/Write checkboxes fire.

### Phase 4 — Secondary actions + persistence

Do **one slice at a time** (each is its own mini-phase).

| Slice | Action | Apple meaning | State |
|-------|--------|----------------|-------|
| **P4a** | FAST / SLOW | max / 1 MHz (zip) | **Done** — UI + worker; ctest |
| **P4b** | TRON / TROFF | Instruction trace file | **Deferred** — wait for shared CPU insn complete record (do not dual-path) |
| **P4c** | TYPE | Apple script: plain text + `\[…]` (OA/CA, B0/B1, J1/J2 axes, RESET, wait) | **Done** — clipboard stays plain text only |
| **P4d** | SWAP | Disk II multi-image queue step (selected slot, d0) | **Done** — machine queue + BP action + host mirror |
| **P4e** | INI round-trip | Load at start / save with config | **Done** — `[DEBUG] break.*` load on start, save on quit |

### Phase 5 — Control port (H2 / remote-debug C1)

Wire product control BP RPCs. **UI path is solid** — remaining work lives in
[`remote-debug.md`](remote-debug.md) phase **C1** (depends on product control
skeleton C0).

### Dependency

```text
Phase 0 → Phase 1 (exec gold)
              ├→ Phase 2 (mapping polish)
              └→ Phase 3 (bus hook / R/W) → Phase 4 (actions) → Phase 5 (control)
                                                          ↘ remote-debug C0 first
```

---

## Implementation notes

| Area | Files |
|------|--------|
| Worker match + commands | `src/runtime/runtime_thread.c` |
| Structs / commands / client | `runtime_event.h`, `runtime_command.h`, `runtime_client.*`, `runtime_internal.h` |
| Conditions | `runtime_breakpoint_condition.*` |
| INI | `runtime_breakpoint_ini.*` |
| Host intents | `src/main.c`, `src/frontend/frontend.c` |
| Tests | `tests/runtime/test_runtime_breakpoint.c`, `test_runtime_breakpoint_ini.c` |
| c64m reference (parked / history) | `main_c64m.c`, git `c4d79b8` thread |

**Rules:** UI never touches live `apple2_t`. Runtime owns table. Match c64m debugger UX for the same 6502 interactions ([`rules.md`](rules.md)).

---

## Status (update when phases land)

| Phase | State |
|-------|--------|
| 0 Product create path | **Done** |
| 1 Exec match pipeline | **Done** |
| 2 Mapping polish | **Done** — composite Apple `VIEW_FLAGS` UI + access-aware match + INI/control round-trip |
| 3 Bus R/W hook | **Done** — `apple2_set_memory_access_callback`; write watchpoint ctest |
| 4 Secondary actions | **P4a–e done** (P4b TRON landed with remote-debug C5b) |
| 5 Control port | **Done** — A2M/3 break-* RPC ([`remote-debug.md`](remote-debug.md) C1) |

### TYPE script language (BP Type field only)

| Form | Meaning |
|------|---------|
| plain text / newlines | `$C000` keys (Return for NL) |
| `\[OA]` `\[OA+]` `\[OA-]` | Open-Apple pulse / hold / release (`CA` same) |
| `\[B0]` `\[B1]` (+/-) | Gameport buttons |
| `\[J1X=n]` `\[J1Y=n]` `\[J2…]` | Axes 0..255 (128 center) |
| `\[J1XL]` `\[J1XR]` `\[J1YU]` `\[J1YD]` `\[J1XC]` `\[J1YC]` | Extremes / center one axis |
| `\[J1C]` `\[J2C]` | Both axes → 128 |
| `\[RESET]` `\[COLDRESET]` | Warm / cold reset |
| `\[W:N]` | Wait N units (~10 ms each at 1 MHz) |

Clipboard **Opt+Insert** remains plain `apple2_paste_begin` (no escapes).

### SWAP (P4d)

| Piece | Behavior |
|-------|----------|
| Machine | Each Disk II drive has a multi-image queue (`DISKII_DRIVE.images`) |
| Populate | Repeat CLI `-d s6d0=a.nib -d s6d0=b.nib` (same drive appends); drop/add host path |
| Action | BP Swap: step queue on the selected **slot 0–7, drive 0** (bare / `0` → next; `+N`/`-N` relative; `N` absolute 1-based). Default slot is 6. A slot without Disk II logs an error and pauses. |
| Event | `RUNTIME_EVENT_DISK_SWAP` (slot + device = drive 0/1) for host UI queue mirror |
| Later | Swap targeting drive 1 |

### INI (P4e)

| Key | Example value |
|-----|----------------|
| `break.E000` | `execute,map,break` |
| `break.C000-C001` | `write,aux,break,swap-slot=5,swap=+1` |
| `break.0801` | `execute,map,break,fast` |

Load: when `use_ini` and `ini_path` are set. Save: on quit after worker stop when `--saveini` or `--remember` (not `--nosaveini`).

**Manual check remaining:** Type field `\[J1XL]CAT\n`; Fast/Slow; R/W STA; multi-disk Swap `+1`; quit with `--remember` keeps breaks.
