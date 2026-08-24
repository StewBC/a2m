# Testing

## Gate

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expect **61** green. Run from repo root.

## Registered tests (product gate)

| Name | Area |
|------|------|
| `audio_buffer` | util SPSC audio |
| `message_queue` | util queues |
| `apple_type_script` | BP TYPE script parser (OA/sticks/RESET) |
| `apple2_file` | NAPS/AppleSingle/legacy detection + Applesoft codec |
| `apple2_stub` | machine init/maps |
| `cpu65_basic` | CPU |
| `softswitch` | banking / LC / kbd / gameport |
| `rom_boot` | //e and ][+ banners |
| `video_beam` | VBL / floating bus / PAGE2 / LORES / DLORES / HGR / 80-col / DHGR / mono bits |
| `video_block_paint` | full-frame block paint (text/hgr/lores/dlores) |
| `diskii` | NIB mount + boot free-run |
| `peripherals` | Mockingboard + SmartPort unit |
| `hostfs` | HostFS NAPS parse/map, nested dirs, file+dir write-through, rescan, access-triggered refresh, CREATE reconcile, `hostfs.order`, mixed mount |
| `cxxx_map` | CXXX / SETC3ROM / INTCXROM / MB hide / C800 latch |
| `memview` | VIEW_FLAGS memory windows |
| `apple2_snapshot` | Machine `.a2state` serialize round-trip |
| `a2m_help` / `a2m_version` / `a2m_headless` | CLI smoke |
| `app_options_mounts` | Disk II / SmartPort / model CLI |
| `runtime_stepping` | step + run_cycles |
| `runtime_display_stop` | stop-path CRT: Override dumps RAM; beam keeps mid-frame raster |
| `runtime_smartport_boot` | INI-style configured SmartPort startup redirects PC to `$Cn00` after mount |
| `runtime_step_nested` | step-over / out / run-to-cursor |
| `runtime_memory_rpc` | token memory claim |
| `runtime_breakpoint` | exec create/enable, composite RAM/C100/D000 mapping, access-aware write watchpoint |
| `runtime_breakpoint_ini` | `[DEBUG] break.*` load + save round-trip |
| `memory_search` | String/hex parsing, case folding, next/previous, wrap, invalid-plane bytes |
| `frontend_input` | Modern Backspace vs original Apple DEL mapping and physical Delete |
| `help_view` | Headless nuklear render of the help overlay: search hit highlighting and the measured scroll correction |
| `runtime_turbo` | turbo CSV MHz/max cycle / set; Configure live ladder apply |
| `runtime_slot_resolve` | prefer-home then scan for Disk II / SmartPort slots |
| `runtime_savestate` | save/load `.a2state` via runtime client |
| `runtime_machine_files` | worker raw/NAPS load-save + Applesoft ASCII round trip |
| `runtime_frame_ring` | ARGB rolling frame ring unit |
| `runtime_history_basic` | Flight recorder free-run records (C3) |
| `runtime_history_commands` | HISTORY_INFO/RECORD/CLEAR (C4a) |
| `runtime_history_query` | FIND/READ/CLOSE + HST1 (C4b) |
| `runtime_history_sessions` | Dual session FIND/NEXT isolation (S0/S1) |
| `runtime_state_changed` | state-changed inform + cursor stale (S3) |
| `runtime_inspector` | TM0: master enable arms HST1 + frame ring; pin-3 no re-arm; zero-budget empty tape |
| `runtime_inspector_replay` | TM2: checkpoint + sealed materialize; media truncate; max kills window |
| `runtime_inspector_mode` | TM3/TMA1: enter/exit NOW; land; read-only; sealed step; control mode/exit |
| `runtime_inspector_bp` | TMA2: one BP list; time-travel run-until hits it or live; leave keeps it |
| `control_protocol` | A2M parse + format (`src/control`) |
| `assembler_*` | expressions/conditionals/loops/macros/scopes/targets/CPU profiles/multifile |
| `runtime_assembler` | live RAM assembly + runtime event path |
| `runtime_assembler_mli` | Assembler MLI launch gate (`$BF00`) + auto-run skip notice |
| `disasm_6502` | disassembler |
| `disasm_pc_lock` | PC-centered disasm wrap ($FFFF/$0000) |

## Deferred (not in gate)

| Test | Why deferred |
|------|----------------|
| full history / HST1 control tests | Expand with remote-debug **C4** wire |
| `frontend_help` | Generated help_view content |

## Fixtures

- `tests/fixtures/Apple DOS 3.3 January 1983.nib` — `diskii` + manual boot
- `tests/fixtures/hostfs/` — NAPS-tagged files + `PRODOS#FF0000` for HostFS (`hostfs` ctest + manual `--smart s7d0=...`)

## Perf smoke

```bash
cmake --build build --target bench_realtime
./build/bench_realtime 2
```

Bar: ≥ 1.0× real-time machine free-run on the reference host (no SDL).
