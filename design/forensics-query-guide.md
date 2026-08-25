# Forensics query guide (verb-first Tab)

| Field | Value |
|-------|-------|
| **Author** | swessels |
| **Date** | 2026-08-24 |
| **Status** | Landed (PR 10 of Forensics UI) |
| **Parent** | [`forensics-ui.md`](forensics-ui.md) |
| **Canonical path** | [`design/forensics-query-guide.md`](forensics-query-guide.md) |

This is the detailed design for **PR 10**. Normative product rules are also restated in [`forensics-ui.md`](forensics-ui.md) (Query line + PR 10) so a c64m port of that doc cannot miss them.

---

## Overview

Replace PR 4's last-token, find-key-only Tab with a **left-to-right walker** over a tiny **verb-first** query language. Tab either unique-completes expected terminals or prints **that hole's** ASCII help on the status strip. When the caret is at the end of the line, Tab also **unique-expands every token** on the line (not only the last).

Control-port `history-find` / `history-read` are unchanged. This is the Forensics query line only. There are no Forensics "clients" to keep compatible: dropping implicit FIND (`address=$4000` with no `find`) is intentional.

---

## Why PR 4 Tab is not enough

`forensics_view_autocomplete` looks at the last whitespace token and always completes against find option keys (and `access=` / `direction=` / `from=` values). It does not know verbs, does not know `read`'s `before=` / `after=` / `epoch=`, and does not teach value syntax. Empty Tab prints a find-key hint. `read`<Tab> is treated as a failed find-key prefix.

PR 9 (caret at end after rewrite) remains required: any buffer rewrite still has to set Nuklear `edit.cursor` explicitly.

---

## Product rules

1. **Always verb-first.** First token must be `find`, `next`, `read`, or `info`. Bare `key=value` is **not** FIND. **Tab** may unique-complete a verb prefix (`f` -> `find `). **Enter** requires an **exact** verb (`f` + Enter does not submit as find). Non-empty line whose first token is not an exact verb: no dispatch, status = verb help (same string as Tab), not generic `bad-args`. Empty Enter stays a no-op.
2. **One walker, two consumers.** Tab (complete / help) and Enter (strict parse) share the same grammar. Tab may leave a line that is not yet submittable (`read before=`); Enter still requires a full valid command.
3. **Unique or teach.** At any hole: prefix of exactly one expected terminal -> rewrite to that terminal. Zero or 2+ matches -> **do not guess**, **do not list which matched**, **do not grow a common prefix** (PR 4 LCP is dropped). After every Tab, **always** refresh the status strip (never leave a stale line). Unique-complete then immediately teach the **new** hole (see Status after Tab).
4. **Whole-line unique-expand when caret is at end.** If and only if the first token is an exact verb or a unique verb prefix, Tab unique-expands every token. If the first token is not a unique verb (`xyz`, `address=$4000`), print verb help and **do not** expand later tokens.
5. **Open values are never completed.** Ids, limits, hex, ranges, opcode lists: Tab only prints syntax help.
6. **UI strings are ASCII-only** (`agents/frontend.md`). Help uses `|` and `->`, never Unicode arrows or ellipses.
7. **Find keys/values stay the shared tables.** `runtime_history_find_option_keys()` / `runtime_history_find_access_names()`. Do not hardcode a parallel find-key list. Verb names, `read`/`next` named keys, and help strings live in the Forensics guide table. Find-key **help** is built from that table. Access **help** stays the short catalog line (not the full name dump).

v1 help is **Tab-driven** (status updates on Tab). Live-per-keystroke hints are optional later; same walker.

v1 caret model is **end of line** (PR 9 already restores caret to end after rewrite). Completing a hole in the middle of the line is out of scope.

---

## Grammar (normative)

Whitespace-separated tokens. `key=value` is one token (`=` is not a delimiter). Case-insensitive names. Values keep the user's spelling until unique-expanded.

```text
query    = find-cmd | next-cmd | read-cmd | info-cmd

find-cmd = "find" { find-kv }
find-kv  = "pc=" range16
         | "address=" range16
         | "access=" access-name
         | "direction=" ( "forward" | "backward" )
         | "limit=" 1..256
         | "from=" ( "oldest" | "newest" | id )
         | "epoch=" u64
         | "timeline=" u32
         | "cycle=" range64
         | "value=" byte
         | "opcodes=" opcode-list

next-cmd = "next" [ "limit=" 1..256 ]

read-cmd = "read" { id | read-kv }
read-kv  = "before=" 0..256
         | "after=" 0..256
         | "epoch=" u64

info-cmd = "info"
```

Value syntax for `find-kv` is exactly the Find option grammar in [`forensics-ui.md`](forensics-ui.md) (shared `runtime_history_parse_find_options`). `id` is a retained record id: decimal or `$` / `0x` hex, `>= 1`.

`find` with no options is a valid command (parser defaults: `direction=backward`, `limit=64`). `next` with no `limit=` uses 64. `read` without `id` is not submittable. `info` takes no arguments; extra tokens are `bad-args` on Enter.

**`read` argument order (Enter):** named keys and `id` may appear in **any order**. Exactly one `id` is required. `read before=2 42` and `read 42 before=2` both submit. A second bare token is `bad-args`. Tab may complete `read bef` -> `read before=` before an id exists; Enter still needs the id.

Duplicate named keys: last wins (same as the shared find parser). Unknown keys / bad values: `bad-args` on Enter.

### Verb table

| Verb | Positionals | Named slots | Enter-valid with no extras |
|------|-------------|-------------|------------------------------|
| `find` | none | find option keys (shared table) | yes (defaults) |
| `next` | none | `limit=` | yes (needs a live FIND cursor; existing check) |
| `read` | `id` (required) | `before=`, `after=`, `epoch=` | no (`read needs id`) |
| `info` | none | none | yes |

---

## Slots and terminals

A **slot** is the hole the walker is sitting in. Expected **terminals** at a slot are the strings Tab may unique-complete.

| Slot | Terminals Tab may complete | Otherwise |
|------|----------------------------|-----------|
| start / no verb | `find`, `next`, `read`, `info` | verb help |
| after `find` (new `key=` or bare prefix) | unused-or-all find keys, completed as `key=` | find-key help |
| `find` value, enum (`access`, `direction`, `from` oldest/newest) | that table | value help |
| `find` value, open (`pc`, `address`, `cycle`, `limit`, `epoch`, `timeline`, `value`, `opcodes`, `from` id) | none | value-syntax help |
| after `next` | `limit=` | next help |
| `next` `limit=` value | none | `limit: 1..256` |
| after `read` (no id yet) | `before=`, `after=`, `epoch=` (keys). Bare text is the id hole, not completable | read help (`<id>` plus those keys) |
| after `read` (id present) | remaining named keys | remaining-keys help, or "Enter to run" if none |
| `read` named value | none (`before`/`after` 0..256, `epoch` u64) | value-syntax help |
| after `info` | none | `info takes no args` |

Prefix match is case-insensitive `starts_with`. A prefix is **unique** when exactly one terminal in that slot's set matches.

Single-letter verb prefixes are unique and **must** complete: `f` -> `find `, `n` -> `next `, `r` -> `read `, `i` -> `info `. That is aggressive; it is specified.

Named-key unique-complete uses **all legal keys for that verb** (duplicates allowed; last wins). Empty-slot **help** omits keys already used, to stay short. Rewrite to **canonical table spelling** (`ADD` -> `address=`).

---

## Walker

No yacc. Table of four verbs, each with ordered positionals and a named-slot list. Algorithm for caret-at-end:

1. Trim? **No** — trailing whitespace is meaningful (empty active token).
2. Split on ASCII whitespace into tokens. If the buffer ends in whitespace, the **active** token is empty; otherwise the active token is the last token. Tokens before that are **complete**.
3. **Verb.** If there is no first token, or the first token is not an exact verb and is not a unique verb prefix: unique-complete the first token if unique among the four verbs (rewrite to `verb` plus trailing space); else status = verb help and stop (still unique-expand nothing else). If first token is an exact verb, or Tab uniquely completed it, enter that verb's production.
4. **Consume complete tokens** left to right in that production:
   - token contains `=`: named slot; remember the key (last-wins map); parse/ignore value for walk purposes (bad values do not block later unique-expand of other tokens).
   - else: fill the next unfilled positional (`read` id). If no positional remains, the token is unexpected (Enter will fail; Tab help = that verb's help).
5. **Unique-expand pass** (caret at end): for every token including active:
   - exact verb: keep (`find` stays `find`).
   - named `prefix=value`: if `prefix` uniquely matches a legal key, rewrite the key to the full name; if the slot is enum and `value` uniquely matches an enum terminal, rewrite the value; if value is open, keep it.
   - bare prefix (no `=`): if unique among expected terminals at that position (verb, or named keys when a positional is not required / already filled), rewrite as `name=` for keys or `name ` for a verb.
   - 0 or 2+ : leave the token unchanged.
6. Rebuild the line with single spaces between tokens. Preserve a trailing space iff the input had trailing whitespace **or** the last rewrite was a verb complete. After `key=` with empty value, the line ends at `=`. Example: `f add=$4000`<Tab> -> `find address=$4000` (verb completed **and** later tokens unique-expanded).
7. **Always** set the status strip from the **resulting** hole (Status after Tab). Do not list which prefixes matched. Do not leave the previous strip.
8. If the line changed, set `query_rewrite_pending` (PR 9 caret-to-end). `forensics_view_autocomplete` returns true **only** when the line rewrote; status may change on a false return.

Enter uses the same verb/slot walk, then strict value parse (shared find parser for `find`; id/`before`/`after`/`epoch`/`limit` checks for the others). **No unique-expand on Enter.** First token must already be an exact verb.

---

## Status after Tab

Unique-complete, then immediately teach the **new** hole. Do not wait for another Tab.

Classify the caret-at-end hole **after** the rewrite:

1. No exact verb -> verb help (`verbs: find | next | read | info`). Same string on **Enter** for a non-empty line whose first token is not an exact verb.
2. Empty active token (trailing space), or `key=` with empty value, or open/ambiguous value -> **that slot's** help (`find add`<Tab> -> `find address=` + `pc/address: ...`; `read bef`<Tab> -> `read before=` + `before/after: 0..256`; `f`<Tab> -> `find ` + find-key help).
3. Last token is complete (finished kv, finished id, exact verb with no trailing space):
   - Command not yet Enter-valid (`read before=32` still missing id) -> that verb's missing-piece help (`read <id> [before=N] ...`).
   - Enter-valid and unused optional named keys remain -> remaining-keys help (unused find keys, or unused `read` keys). Example: `find add=$4000 acc=re`<Tab> -> `find address=$4000 access=read` + unused find-key help.
   - Enter-valid and no unused optional named keys (`info`, `read` with id and all three keys present, `next` with `limit=` already set) -> `Enter to run`.

`next` with no `limit=` is Enter-valid; unused optional key remains, so help is `next [limit=1..256]` (same as empty next), not `Enter to run`. `find` always has unused optional keys unless every find key is already present.

---

## Tab rewrite shapes

| Kind | Rewrite |
|------|---------|
| unique verb | `find ` (trailing space so the next Tab is in that verb) |
| unique key | `address=` (keep any value after `=`) |
| unique enum value | `read` / `backward` / `oldest` (no `=`) |
| open value | no rewrite |

---

## Help catalog (status strip, ASCII, one line)

Must fit `FRONTEND_FR_STATUS_MAX` (192). Truncate with `...` if a later table grows.

| Hole | Status (exact spirit; wording may tighten in code) |
|------|-----------------------------------------------------|
| no verb / ambiguous / garbage first token | `verbs: find \| next \| read \| info` |
| `find` next key (empty or ambiguous) | `find keys: pc= address= access= direction= limit= from= epoch= timeline= cycle= value= opcodes=` |
| `access=` | `access: name (write, data-read, execute, ...); unique prefix completes` |
| `direction=` | `direction: forward \| backward` |
| `from=` | `from: oldest \| newest \| id` |
| `pc=` / `address=` | `pc/address: u16 or lo-hi ($hex ok)` |
| `cycle=` | `cycle: u64 or lo-hi (decimal; 0x ok)` |
| `limit=` (find or next) | `limit: 1..256` |
| `epoch=` | `epoch: u64 (decimal or 0x)` |
| `timeline=` | `timeline: u32` |
| `value=` | `value: byte (dec, $NN, 0xNN; hex ? nibble)` |
| `opcodes=` | `opcodes: A9,??,8D (no $; ? nibble)` |
| `next` empty | `next [limit=1..256]` |
| `read` (id missing) | `read <id> [before=N] [after=N] [epoch=N]` |
| `read` (id present, keys remain) | `read keys: before= after= epoch=` (omit used) |
| `before=` / `after=` | `before/after: 0..256` |
| `info` extra / trailing space after `info` | `info takes no args` |
| Enter-valid, no unused optional keys | `Enter to run` |

Do not dump the full access-name table into the status strip; unique prefix still completes from the shared table.

---

## Worked examples (caret at end)

| Input + Tab | Line after | Status |
|-------------|------------|--------|
| (empty) | (unchanged) | verb help |
| `f` | `find ` | find-key help |
| `n` / `r` / `i` | `next ` / `read ` / `info ` | next / read / `info takes no args` |
| `re` | `read ` | read help |
| `xyz` | (unchanged) | verb help (no later-token expand) |
| `address=$4000` | (unchanged) | verb help (not implicit FIND; no later-token expand) |
| `f add=$4000` | `find address=$4000` | unused find-key help |
| `find add` | `find address=` | `pc/address: u16 or lo-hi ($hex ok)` |
| `find a` | `find a` | find-key help (`address` and `access`) |
| `find add=$4000 acc=re` | `find address=$4000 access=read` | unused find-key help (`re` -> `read` unique; `r` is not: `read` and `rmw-dummy-write`) |
| `find a=$4000 acc=re` | `find a=$4000 access=read` | unused find-key help (`a` not unique) |
| `find add=$4000 acc=r` | `find address=$4000 access=r` | access-value help (`r` ambiguous) |
| `read bef` | `read before=` | `before/after: 0..256` |
| `read 12345 ` (trailing space) | `read 12345 ` | remaining read keys |
| `read before=` + Enter | (no dispatch) | `bad-args (read needs id)` |
| `read before=2 42` + Enter | (dispatch read id=42 before=2) | (order-independent id) |
| `address=$4000` + Enter | (no dispatch) | verb help (same string as Tab) |
| `info ` | `info ` | `info takes no args` |
| `info` (no trailing space) | `info` | `Enter to run` |
| `next lim` | `next limit=` | `limit: 1..256` |

Access-name uniqueness notes for implementers: `re` matches only `read`; `r` matches `read` and `rmw-dummy-write`. `data` matches `data`, `data-read`, `data-write`.

---

## Implementation sketch

- Keep the name `forensics_view_autocomplete` (walker behind it). Return true **only** when the query text rewrites; always refresh status. Same Tab key in `frontend_handle_forensics_key`.
- Submit parse in `forensics_view_parse_query`: drop implicit FIND; first token must be an exact verb or fail with verb help (not generic `bad-args`); `info` with extra tokens is `bad-args`; `read` accepts id and named keys in any order (exactly one id).
- Data: static verb table in `forensics_view.c`; find keys/access from `runtime_history_query_parse`. Find-key help built from `runtime_history_find_option_keys()`. `direction` / `from` enum lists can stay local (already in PR 4) or move next to the verb table. `from=` still unique-completes `oldest` / `newest`; a numeric `from=` value is an open id.
- Rebuild `state->query`; on any change set `query_rewrite_pending` (PR 9 caret).
- Tests in `tests/frontend/test_forensics_view.c`: table of (input, after-Tab line, status substring) from this guide; implicit FIND rejected on Enter; `info foo` rejected. Headless caret-at-end on a **verb-first** rewrite (`find ad` -> `find address=`, not bare `ad`). Update the existing `acc` / `ad` Tab cases the same way.
- Manual **Forensics** query table: remove "or bare `key=value...`"; document Tab as guided complete / help. Run `tools/gen_help.py`.
- Fold the query-line rule into `agents/frontend.md` in the same change (today it still says Tab autocompletes from find-option keys).
- ASCII-only status strings (`agents/frontend.md`). This design is landed with PR 10.

---

## Non-goals

- Changing control-port `history-find` (bare keys remain valid on the wire).
- Live help on every keystroke (v1 = Tab).
- Caret in the middle of the line.
- Filtered "you matched find and from" lists; LCP growth.
- Completing numeric / hex / range values.
- Discoverability beyond the status strip (no popup list).

---

## Relation to Forensics PRs

| PR | Role |
|----|------|
| 4 | FIND/NEXT/READ + last-token find-key Tab (superseded here) |
| 9 | Caret at end after rewrite (still required) |
| 10 | This document: verb-first grammar, walker, whole-line unique-expand, slot help |
