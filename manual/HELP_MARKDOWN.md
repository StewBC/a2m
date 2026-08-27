# In-Emulator Help Markdown

`manual.md` is the shared source for external documentation and the compiled
in-emulator help view.

The emulator help renderer supports a small Markdown subset:

- one leading `#` document heading;
- `##` headings for help sections;
- `###` and deeper headings as subsection headings inside a section;
- paragraphs, blank lines, bullets, numbered items, fenced code blocks, inline
  backtick code spans, and simple pipe tables.

Pipe tables are flattened into aligned text columns for the emulator. Avoid
Markdown constructs such as HTML, images, blockquotes, and nested lists when the
content must appear correctly in-emulator.

Inside table cells, escape a literal `|` as `\|` so Markdown does not treat it
as a column separator (e.g. `` `enh\|plus` ``). `tools/gen_help.py` strips that
backslash when compiling in-emulator help.

Use only ASCII characters. Unicode characters such as '→', ellipsis '…', Em
dash '—' (U+2014), or En dash '–' (U+2013) are not allowed.
`tools/gen_help.py` fails the build if it finds any non-ASCII character in
`manual.md`.
