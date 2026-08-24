# Design documents

This directory holds **design docs** for a2m: proposed or in-flight architecture
writeups that are not yet (or no longer) part of the agent handoff surface.

## Why not `agents/`?

[`agents/`](../agents/) is the implementation brief for the product **as it is**.
Design drafts, alternatives, and abandoned plans clutter that handoff and go
stale relative to source. Keep speculative and program-of-work docs here;
promote lasting invariants into `agents/*.md` and `manual/manual.md` when the
work lands.

The agents index points here: [`agents/README.md`](../agents/README.md) → Design docs.

## Status legend

| Status | Meaning |
|--------|---------|
| **active** | Current design work; last touched for an in-progress feature |
| **landed** | Implemented; doc retained for history / follow-ons |
| **abandoned** | Not pursuing; kept only if useful as negative knowledge |

## Index

| Design | Status | Last worked on | Path |
|--------|--------|----------------|------|
| Forensics UI (flight recorder / HST1 FIND) | **active** | 2026-08-24 | [`forensics-ui.md`](forensics-ui.md) |

## Conventions

- One topic per file; kebab-case names.
- Start with Title & Metadata (author, date, Draft/Accepted).
- Cite real source paths and APIs; prefer Mermaid for flows.
- When implementation merges, update this index (active → landed) and fold
  durable rules into the relevant `agents/` note in the same change set when
  practical.
