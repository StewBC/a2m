# Golden rules

Short, non-negotiable constraints. Prefer these over inventing new structure.

## Threads and ownership

| Owner | Owns |
|-------|------|
| **UI / main thread** | SDL, frontend, option lifecycle, host chords |
| **Runtime thread** | Live `apple2_t`, execution, breakpoints, paint, audio produce (interleaved stereo L,R; free-run advances AY without host PCM) |
| **Audio callback** | Read-only pull from the audio buffer (when running) |

- Consumers get **copied** snapshots, frames, and memory.  
- **No live `apple2_t *` across the thread divide** (not in queues, not in the UI).  
- UI and future control talk only through **`runtime_client`** (commands + events).

## Dependency direction

```text
frontend  →  runtime + platform + machine(headers) + tools + util
runtime   →  machine + tools + util
machine   →  util only
main      →  app_options + frontend + runtime_client + platform
```

- **Machine must not** include SDL, Nuklear, or frontend headers.  
- **Runtime must not** include frontend headers.  
- Do not add reverse dependencies.

## Where code lives

| Path | Owns |
|------|------|
| `src/machine/` | CPU, soft switches, video beam/paint, Disk II, SmartPort, MB, ROMs |
| `src/runtime/` | Worker, client, frame slot, commands/events |
| `src/frontend/` | Debugger UI, layout, CRT, disk LEDs, host stick tables |
| `src/main.c` | Host loop, key chords, intent dispatch, gameport host |
| `src/app_options.*` | CLI / INI |
| `src/platform/` | SDL window / input / audio / fs |
| `src/control/` | Product A2M control (wire + dispatch) |

## Product shell rules

1. **Host loop:** keep the **c64m skeleton** in `main.c`. Apple substitutions only
   (media, banking labels, keys, gameport).
2. **Debugger UX:** if a2m and c64m differ for the same 6502-class debugger
   interaction, **a2m is wrong** until it matches — except Apple-only surfaces.
3. **Never reintroduce** C64 product metaphors (1541 drives 8/9, PRG/KERNAL, CIA
   joystick as the machine model).
4. **Apple NTSC-first** video timing; Franklin/Videx 80-col card and a2m text UI
   are **non-goals**.

## Video rules (paint work)

1. **Keep the beam:** Φ0 step → paint → advance H/V. Do not return to
   “repaint whole RAM on a timer” as the primary path.
2. **Keep the host contract:** runtime publishes ARGB frames; UI presents them.
   Change size only with an explicit `display_frame` / frontend update (see
   [`video-paint.md`](video-paint.md)).
3. **Paint is a replaceable backend.** a2m-class modes land first; NTSC artifact
   later swaps the **decoder**, not the product shell.
4. Preserve **timing-visible** side effects (VBL, floating bus, mid-frame switches)
   when optimizing.

## Diagnosis discipline

1. Observables first (where / when / what differs).  
2. One kill criterion per hypothesis.  
3. Ground truth: real hardware ≥ trusted emulators (AppleWin, a2m) and notes ≥ our tests.  
4. If a test encodes wrong physics, rewrite the test.  
5. Prove blast radius on boot + known demos after a fix.

## Build defaults

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run from **repo root** so fixtures and relative disk paths resolve.
Use `--noini` for reproducible smokes.
