# Testing

## Gate

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expect **68** green. Run from repo root.

## Registered tests

| Name | Area |
|------|------|
| `audio_buffer` | util SPSC audio |
| `message_queue` | util queues |
| `config` | util INI load/save; section consolidation on write |
| `fs_watch` | native filesystem notifications + bounded loss handling |
| `apple_type_script` | BP TYPE script parser (OA/sticks/RESET) |
| `apple2_file` | NAPS/AppleSingle/legacy detection + Applesoft codec |
| `apple2_stub` | machine init/maps |
| `cpu65_basic` | CPU |
| `softswitch` | banking / LC / kbd / gameport |
| `rom_boot` | //e and ][+ banners |
| `video_beam` | VBL / floating bus / PAGE2 / LORES / DLORES / HGR / 80-col / DHGR / mono bits |
| `video_block_paint` | full-frame block paint (text/hgr/lores/dlores) |
| `video_pixel_address` | soft-switch-locked pixel → bank/ofs/adr (text/lores/hgr/dhgr/mixed) |
| `crt_pixel_map` | CRT barrel round-trip + mouse→Apple pixel (flat and curved) |
| `diskii` | NIB mount + boot free-run |
| `peripherals` | Mockingboard + SmartPort unit |
| `hostfs` | HostFS NAPS, nested dirs, write-through, rescan, `hostfs.order` |
| `cxxx_map` | CXXX / SETC3ROM / INTCXROM / MB hide / C800 latch |
| `memview` | VIEW_FLAGS memory windows |
| `apple2_snapshot` | Machine `.a2state` serialize round-trip |
| `a2m_help` / `a2m_version` / `a2m_headless` | CLI smoke |
| `app_options_mounts` | Disk II / SmartPort / model CLI |
| `runtime_stepping` | step + run_cycles |
| `runtime_display_stop` | stop-path CRT: Override dumps RAM; beam keeps mid-frame raster; paused memory write refreshes CRT |
| `runtime_smartport_boot` | configured SmartPort startup redirects PC to `$Cn00` |
| `runtime_step_nested` | step-over / out / run-to-cursor |
| `runtime_memory_rpc` | token memory claim |
| `runtime_breakpoint` | exec create/enable, composite mapping, write watchpoint |
| `runtime_breakpoint_ini` | `[DEBUG] break.*` load + save round-trip |
| `memory_search` | String/hex parsing, wrap, invalid-plane bytes |
| `frontend_input` | Backspace vs original Apple DEL; physical Delete |
| `help_view` | Headless nuklear help overlay (search hits + scroll correction) |
| `forensics_view` | Forensics shell state (open/close latch, query history, clear) |
| `runtime_turbo` | turbo CSV MHz/max; Configure live ladder apply |
| `runtime_slot_resolve` | prefer-home then scan for Disk II / SmartPort |
| `runtime_savestate` | save/load `.a2state` via runtime client |
| `runtime_machine_files` | worker raw/NAPS load-save + Applesoft ASCII |
| `runtime_frame_ring` | ARGB rolling frame ring |
| `runtime_history_basic` | Flight recorder free-run records |
| `runtime_history_commands` | HISTORY_INFO/RECORD/CLEAR |
| `runtime_history_query` | FIND/READ/CLOSE + HST1 |
| `runtime_history_query_parse` | Shared history-find option grammar + key tables |
| `runtime_history_wire_decode` | HST1 decode round-trip + Python `Ctl.decode_hst1` golden |
| `runtime_history_sessions` | Dual session FIND/NEXT isolation |
| `runtime_state_changed` | state-changed inform + cursor stale |
| `runtime_inspector` | master enable arms HST1 + frame ring; pin-3 no re-arm |
| `runtime_inspector_replay` | checkpoint + sealed materialize; media truncate; max wipes window |
| `runtime_inspector_mode` | enter/exit NOW; land; read-only; sealed step |
| `runtime_inspector_bp` | one BP list; time-travel run-until hits it or live |
| `control_protocol` | A2M parse + format (`src/control`) |
| `assembler_*` | expressions/conditionals/loops/macros/scopes/targets/CPU profiles/multifile |
| `runtime_assembler` | live RAM assembly + runtime event path |
| `runtime_assembler_mli` | MLI launch gate (`$BF00`) + auto-run skip notice |
| `disasm_6502` | disassembler |
| `disasm_pc_lock` | PC-centered disasm wrap ($FFFF/$0000) |
| `am65_cli_65c02` / `am65_cli_wdc` | standalone `am65` CPU profiles |
| `am65_cli_default_rejects_65c02` / `am65_cli_rockwell_rejects_wdc` | WILL_FAIL (wrong profile) |

A leftover `test_runtime_timemachine*` binary in `build/` is not in the gate.

## Not in the gate

| Item | Why |
|------|-----|
| Dedicated `frontend_help` content unit | Overlay is covered by `help_view`; CMake notes a content test if the public help API grows |

## Fixtures

- `tests/fixtures/Apple DOS 3.3 January 1983.nib` — `diskii` + manual boot
- `tests/fixtures/hostfs/` — NAPS-tagged files + `PRODOS#FF0000` for HostFS

Gate tests must not require gitignored or untracked media. Optional local
samples (`disks/`, `samples/hostfs/pt3plr`, …) may be exercised when a complete
tree is present; otherwise those checks skip. Do not fail the gate on a partial
local sample.

## Perf smoke

```bash
cmake --build build --target bench_realtime
./build/bench_realtime 2
```

Bar: ≥ 1.0× real-time machine free-run on the reference host (no SDL).
