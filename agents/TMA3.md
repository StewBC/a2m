# TMA3 — Max turbo wipes Record (addendum)

**Status:** Landed.  
**Epic:** [`timemachine.md`](timemachine.md)  
**Prev / Next:** [`TMA2.md`](TMA2.md) / —  
**Depends on:** TM2 max policy (`history_off_on_max`); TMA1 Inspector timeline.

Related: [`TM2.md`](TM2.md) (original “stop on enter, truncate on leave”) · [`TMA0.md`](TMA0.md).

This is a **layer**. Do not rewrite TM2.md Landed. TM2’s max pin was: recording
stops in max; leave-max truncates `tm_window` to the `RECORDER_RESUME` marker.
That left pre-max snapshots sitting under a live cycle that kept running, so
Inspector Duration grew without cap and releasing the scrubber landed on the
last checkpoint (slider slammed left). This addendum is the product shape for
c64m transcription.

**Not going to TM6.** Finite turbo (1 / 4 / 8 MHz) still records.

---

## Shape (pinned)

Default `history_off_on_max` (true). **Max** only.

1. **Enter max** — remember whether Record was on.  
2. **Wipe** the TimeMachine tape (checkpoints + input log; film ring if Record
   was on) and **turn Record off**. Inspector looks like Record is off: no
   Duration, no Inspect, no scrubber. Record checkbox is locked. No help
   dump on the tab.  
3. **Leave max** — restore the remembered Record state.  
4. **If Record was on**, start recording again into an **empty** window from
   that moment.

If Record was already off, it stays off across the round-trip. `--no-history-off-on-max`
is the opt-out (keep recording in max).

A Record click **while in max** does not start a tape. Enable is remembered for
leave-max; disable clears that memory. The checkbox stays locked off in the UI.

If Inspect is active when max is entered, leave Inspect (restore NOW) first,
then wipe.

---

## Why

Inspector Duration is `(live − oldest snapshot) / (17030×60)` guest seconds.
Live is current `apple2_cycles`. With the recorder stopped but old snapshots
kept, live ran away and Duration was unrecorded max-run time. Preview used the
frame ring (still filling in max); land used the last checkpoint (frozen). Those
clocks disagreed.

Wiping on **enter** matches the advertised discard (one Opt+T throws the tape
away) instead of discarding only on leave.

---

## Non-goals

- Recording through max (that is the opt-out flag)  
- Changing D10 media-write cuts  
- Finite-MHz turbo discarding history  
- Dual windows / islands (D17 still one interval)  
- Renaming `mode=forensic`

---

## Landed

- Worker: `runtime_history_apply_max_policy` remembers `tm_enabled_saved_for_max`,
  wipes via `runtime_tm_on_history_invalidate`, forces `timemachine_enabled`
  false, restores with `runtime_tm_set_enabled(true)` on leave if saved.  
- `runtime_tm_set_enabled` while already on max with policy on does not arm the
  recorder.  
- Inspector: Record locked while in max; no explanatory banner.  
- Gate: `runtime_tm_replay` pins wipe-in-max, restore-on-leave, Record-off
  stays off.  
