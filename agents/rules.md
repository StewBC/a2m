# Golden rules

Short, non-negotiable constraints. Prefer these over inventing new structure.

## Threads and ownership

| Owner | Owns |
|-------|------|
| **UI / main thread** | SDL, frontend, option lifecycle, host chords |
| **Runtime thread** | Live `apple2_t`, execution, breakpoints, paint, audio produce (interleaved stereo L,R; max free-run advances AY without host PCM) |
| **Audio callback** | Read-only pull from the audio buffer (when running) |

- Consumers get **copied** snapshots, frames, and memory.
- **No live `apple2_t *` across the thread divide** (not in queues, not in the UI).
- UI and control talk only through **`runtime_client`** (commands + events).

## Dependency direction

```text
frontend  →  runtime + platform + machine(headers) + tools + util
runtime   →  machine + tools + util
machine   →  util only
main      →  app_options + frontend + runtime_client + platform
control   →  runtime_client + platform sockets
```

- **Machine must not** include SDL, Nuklear, or frontend headers.
- **Runtime must not** include frontend headers.
- Do not add reverse dependencies.

## Where code lives

| Path | Owns |
|------|------|
| `src/machine/` | CPU, soft switches, video beam/paint, Disk II, SmartPort, HostFS, MB, ROMs |
| `src/runtime/` | Worker, client, commands/events, breakpoints, history, TimeMachine |
| `src/frontend/` | Debugger UI, layout, CRT, disk LEDs, host stick tables, help overlay |
| `src/main.c` | Host loop, key chords, intent dispatch, gameport host |
| `src/app_options.*` | CLI / INI |
| `src/platform/` | SDL window / input / audio / fs / sockets |
| `src/control/` | A2M control wire + dispatch |
| `src/tools/am65/` | Shared assembler library + `am65` CLI |

## Product

This is an **Apple II** emulator. Do not reintroduce C64 product metaphors
(1541 drives 8/9, PRG/KERNAL, CIA joystick as the machine model).

Apple **NTSC-first** video timing. Franklin/Videx 80-col cards and an a2m text
UI are non-goals.

## Video

1. **Keep the beam:** Φ0 step → paint → advance H/V. Do not return to
   “repaint whole RAM on a timer” as the primary path. Max turbo is the
   documented exception (A-lite counters + ~60 Hz block paint).
2. **Keep the host contract:** runtime publishes ARGB **560×192** frames; UI
   presents them. Change size only with an explicit `display_frame` /
   `APPLE2_VIDEO_*` update (they are static-asserted together).
3. **Paint is a replaceable backend.** Current modes are a2m-class colour /
   discrete-bit mono. An NTSC artifact decoder later swaps the **decoder**,
   not the product shell.
4. Preserve **timing-visible** side effects (VBL, floating bus, mid-frame
   switches) when optimizing.

## Time travel

- One true `apple2_t`. Inspect replaces it with reconstructed past; Leave
  restores live NOW. See [`timemachine.md`](timemachine.md).
- Inspect is **read-only**. Forward motion is sealed re-execute, clamped to
  live. No reverse CPU. No write-delta stream.

## Diagnosis

1. Observables first (where / when / what differs).
2. One kill criterion per hypothesis.
3. Ground truth: real hardware ≥ trusted emulators (AppleWin) ≥ our tests.
4. If a test encodes wrong physics, rewrite the test.
5. Prove blast radius on boot + known demos after a fix.

## User-facing docs

If a user-visible feature is added or changed, update `manual/manual.md` in
the same change. Read `manual/HELP_MARKDOWN.md` first. Do not link the manual
at `agents/`.

## Build defaults

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run from **repo root** so fixtures and relative disk paths resolve.
Use `--noini` for reproducible smokes.
