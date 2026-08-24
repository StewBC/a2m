# TimeMachine / Inspector

Runtime-owned time travel: checkpoints + input log + sealed re-execute.
The UI is Misc → **Inspector**. HST1 is a **separate** flight recorder (FIND),
not the Inspector slider.

Files: `src/runtime/runtime_inspector.*`, `runtime_inspector_recorder.c`,
`runtime_history.*`, `runtime_frame_ring.*`. UI: `frontend.c`
(`frontend_draw_misc_inspector`).

## Two streams (do not conflate)

| Stream | What it is | Product use |
|--------|------------|-------------|
| **TimeMachine** | Checkpoint snapshots + input log + sealed re-execute | Time travel. Slider, land, `±`, F10-family. |
| **HST1** | Instruction log | FIND: “who wrote `$22` to `$2011`”. Not the slider, not F12. |

## How it works

**Record is opt-in.** `[debug] inspector=1` / `--inspector` / the Record
checkbox. Default **off** (play Total Replay with no tape cost). On →
checkpoints every **20000** cycles (`RUNTIME_INSPECTOR_CHECKPOINT_CADENCE_CYCLES`),
input log, frame-ring film, HST1 as configured. Budget:
`inspector_memory_mb` (default 128).

A checkpoint is `apple2_snapshot_save` into a ring slot. Reconstruct =
load nearest checkpoint ≤ target, then **re-execute sealed** to that cycle.
Backward is the same from an earlier checkpoint — **not** reverse-CPU.

**Inspect** (enter): requires checkpoints. Film is optional. Starts at **live**
(the NOW blob taken on enter). Machine becomes read-only. `get-memory` /
`get-cpu` see THEN.

**Land** (slider release): load last checkpoint ≤ that time and paint. Far
right = restore the NOW blob, not the last cadence checkpoint. Thumb-down is
**preview only** (film blit, or pink CRT if no still); the Apple does not move
until release.

**`[-]` / `[+]`**: one guest video frame, clamped to oldest snapshot / live.

**F10 / F11 / Shift+F10 / F12 / Shift+F12**: re-execute on the landed Apple.
F12 runs until a **breakpoint** or **live**, then **stops**, still in Inspect.
No-op at live. Opt+Left is unbound. Pokes reject (`read-only-inspector`).

**Leave**: restore live NOW, still **paused**. Does not auto-resume.

**One breakpoint list.** Opt+B and the Breakpoints tab always edit it. Time-travel
F12 hits those breakpoints.

**CRT on stop:** any F10-family / F12 / Pause that leaves the Apple stopped
must publish a CRT frame. Override on (Hardware tab) dumps video RAM; Override
off publishes the beam buffer. Paint-off (max turbo, sealed F12) has no beam
image: dump RAM.

Window headers are dark cobalt while inspecting. Do not tint the panel fill.

Control wire: `get-state` reports `mode=live|inspector`; **`leave-inspector`**
exists; there is **no** enter/land/seek verb (UI uses `runtime_client`). FIND
stays (`history-find` / `history-next` / `history-read`).

## Max turbo

Default `history_off_on_max` (true):

1. Enter max — remember whether Record was on; leave Inspect if active.
2. Wipe the tape (checkpoints + input log; film if Record was on) and turn
   Record off. Checkbox locked.
3. Leave max — restore the remembered Record state into an **empty** window.

Finite MHz still records. `--no-history-off-on-max` keeps recording in max.
A Record click while in max does not start a tape; enable is remembered for
leave-max.

## Media writes

A **guest** media write that succeeds **cuts the window**: records older than
the write are dropped; recording continues forward. A refused write must not
cut. Housekeeping (eject flush, snapshot flush) must not cut. HostFS directory
refresh is a different marker (`HOST_DIRECTORY`). Disk II mechanical state
rides in the checkpoint; media *bytes* live in the host file, so a replayed
read would otherwise return present-day bytes.

One interval, never islands: a cut moves `oldest` to the marker.

## Sealed replay

CPU observer off, memory-access callback off, no frame push / publish, no
`runtime_produce_audio`. Media write suppression is a safety net (the window
never spans a write). A leaky seal corrupts the tape being stood on. Mockingboard
chip state in the checkpoint is correct; host audio is muted during replay.

Join key for film vs machine is **`machine_cycle`**. Frames and HST1 are not
one lock-step stream. Index retained frames by **slot**, not by frame number
(numbers have gaps). Under turbo, paints get rarer while history stays
instruction-dense — do not synthesize fake frames to even out the scrubber.

## Do not re-open

- A **write-delta** stream instead of checkpoints. Measured: a checkpoint is
  ~160K; cadence 20k cycles bounds replay to well under a millisecond;
  re-execute reproduces beam, Disk II mechanics, and VIA/AY for free.
- Reverse-execution of the 6502.
- Driving the Inspector slider or time-travel F10 from **HST1**.
- Dual NOW+THEN panels.
- Always-on recording by default.
- Using live `step-instruction` to move a time-travel cursor (that mutates
  the machine).

Promote / Branch (“make this past the new live”) is not implemented — see
[`known-gaps.md`](known-gaps.md) if asked.

## Tests

`runtime_inspector`, `runtime_inspector_replay`, `runtime_inspector_mode`,
`runtime_inspector_bp`, plus history / frame-ring tests in [`testing.md`](testing.md).
