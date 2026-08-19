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

Use only ASCII characters. Unicode characters such as '→' and Em dash '—' 
(Unicode: U+2014) or En dash '–' (Unicode: U+2013), for example, are not allowed.