# Machine snapshots

Durable Apple II snapshots: **`.a2state`** only (drop, `--sna`, quicksave,
control, Machine tab).

Serialize on the **runtime thread**. Host never touches `apple2_t`.

## Surfaces

| Surface | Path |
|---------|------|
| Drop `.a2state` on the window | `main.c` `handle_drop_file` → `runtime_client_load_state` |
| `--sna path` | After start + mounts |
| Opt+Shift+`.` / `,` | Quicksave / newest in the snapshot browse folder |
| Control | `save-state` / `load-state` |
| Misc → Machine | Unified Load/Save |

## Machine blob

`src/machine/apple2_snapshot.c`: little-endian magic **`A2ST`** (`0x41325354`),
**version 2** (loads v1+). Chunked; not `memcpy` of `apple2_t`.

| Tag | Contents |
|-----|----------|
| `META` | flags, content mode, model, `mb_slot` |
| `CPU_` | CPU + micro state; **v2** trailing `uint32_t prng` (v1 load keeps seed `0xA2A2A2A2`) |
| `RAM_` | Full **128K main + 32K LC** (][+ unused half is zeros) |
| `SOFT` | `state_flags`, key, strobed_slot, speaker, gameport |
| `VID_` | Beam H/V, frame_number/gen, last_video_byte, paint_enabled — **not** framebuffer / mono / phosphor |
| `SLOT` | per-slot type + diskii_present + mb_slot |
| `DSKs` | Disk II path queue + mechanical state |
| `SPrt` | SmartPort paths + handshake buffer |
| `MBrd` | VIA + AY chip state |

**Referenced media only.** `A2_SNAPSHOT_CONTENT_SELF_CONTAINED` exists as an
enum and is not written. Missing media path on load is a **hard failure**.
Dirty Disk II images are flushed to their files before save; a failed flush
fails the save.

Never serialize: host pointers, page maps, framebuffer, paste, write_history,
mono/phosphor. After load: `softswitch_apply_full_map`, rebind CPU callbacks,
paint one frame.

Host `main.c` may append a **HOST** trailer (window/layout) outside the machine
blob. That is not part of `apple2_snapshot_*`.

## Runtime policy

On load: clear CPU history and frame ring (cycle epochs are wrong); invalidate
the Inspector tape; clear paste; **preserve** was-running vs paused.
Breakpoints are **not** inside the machine snapshot (they survive load; the
PC may no longer make sense).

Save prefers an instruction boundary when the CPU is mid-instruction.

## Tests

`apple2_snapshot` (machine round-trip, bad magic/version), `runtime_savestate`.
