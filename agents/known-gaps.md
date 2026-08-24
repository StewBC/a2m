# Known gaps (not now)

Read this only if asked. These are true of the code today and are **not**
next work. Do not reopen them as implicit TODOs.

| Gap | Reality |
|-----|---------|
| Promote / Branch | There is no “make this past the new live NOW” verb. History `timeline` exists for discontinuities; no promote API or control op. |
| Self-contained snapshots | `A2_SNAPSHOT_CONTENT_SELF_CONTAINED` is an unused enum. `.a2state` stores **paths**; missing media fails the load. |
| WOZ writes | `.woz` mounts read; `image_put_byte` fails. |
| SmartPort command set | Host trap implements STATUS / READ_BLOCK / WRITE_BLOCK only. No full DIB / extended SP firmware. |
| Extra slot cards | Disk II, SmartPort, Mockingboard. No SSC, mouse, clock, Videx, Franklin. |
| NTSC composite artifact | Current paint is a2m-class colour + discrete-bit mono. A later decoder would swap the paint backend, not the shell. |
| Frame-ring film | Kept as a preview cache (pink CRT where there is no still). Whether that ARGB budget is worth keeping is undecided; the checkpoint is the source of truth. |
| Control enter/land | Wire has `leave-inspector` only. Enter / land / frame-step are `runtime_client` / UI. |
| BP Swap drive 1 | Swap action targets drive 0. |

Closed debates (do not re-litigate without new measurements) live in
[`timemachine.md`](timemachine.md) (write-delta stream, reverse CPU, HST1 as
the Inspector slider, always-on recording).
