#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


ORDERED_RE = re.compile(r"^(\d+)[.)]\s+(.*)$")
HEADING_RE = re.compile(r"^(#{1,6})\s+(.*)$")
INLINE_CODE_ON = "\x01"
INLINE_CODE_OFF = "\x02"


def fail(message):
    raise SystemExit(f"gen_help.py: {message}")


def c_string(value):
    out = []
    for ch in value:
        code = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif code < 32 or code == 127:
            out.append(f"\\{code:03o}")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def strip_inline_markup(text, line_no=None):
    text = re.sub(r"!\[([^\]]*)\]\([^)]+\)", r"\1", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = text.replace("**", "").replace("__", "")
    out = []
    in_code = False
    for ch in text:
        if ch == "`":
            out.append(INLINE_CODE_OFF if in_code else INLINE_CODE_ON)
            in_code = not in_code
        else:
            out.append(ch)
    if in_code:
        where = f" at line {line_no}" if line_no is not None else ""
        fail(f"unmatched inline code backtick{where}")
    text = "".join(out)
    return text


def split_top_level_title(title):
    if "-" not in title:
        return title, title
    section_title, heading = title.split("-", 1)
    return section_title.rstrip(), heading.lstrip()


def add_span(sections, kind, text="", line_no=None):
    if not sections:
        sections.append({"title": "Introduction", "spans": []})
    sections[-1]["spans"].append((kind, strip_inline_markup(text, line_no)))


def add_heading_span(sections, title, line_no=None):
    add_span(sections, "HELP_SPAN_H3", title, line_no)


def visible_len(text):
    return len(text.replace(INLINE_CODE_ON, "").replace(INLINE_CODE_OFF, ""))


def is_table_separator(cells):
    if not cells:
        return False
    for cell in cells:
        if not re.match(r"^:?-{3,}:?$", cell.strip()):
            return False
    return True


def split_table_row(line):
    text = line.strip()
    cells = []
    cell = []
    in_code = False

    for i, ch in enumerate(text):
        if ch == "`":
            in_code = not in_code
            cell.append(ch)
        elif ch == "|" and not in_code:
            if i != 0:
                cells.append("".join(cell).strip())
            cell = []
        else:
            cell.append(ch)

    if cell or not text.endswith("|"):
        cells.append("".join(cell).strip())
    return cells


def flush_table(sections, table_lines):
    rows = []
    widths = []

    if not table_lines:
        return

    for line_no, line in table_lines:
        cells = split_table_row(line)
        if is_table_separator(cells):
            continue
        cells = [strip_inline_markup(cell, line_no) for cell in cells]
        rows.append(cells)
        while len(widths) < len(cells):
            widths.append(0)
        for i, cell in enumerate(cells):
            widths[i] = max(widths[i], visible_len(cell))

    for row_index, cells in enumerate(rows):
        parts = []
        for i, cell in enumerate(cells):
            parts.append(cell + (" " * (widths[i] - visible_len(cell))))
        if parts:
            add_span(
                sections,
                "HELP_SPAN_TABLE_HEADER" if row_index == 0 else "HELP_SPAN_TABLE",
                "  ".join(parts))


def parse_manual(path, section_level=2):
    lines = path.read_text(encoding="utf-8").splitlines()
    heading = None
    sections = []
    in_code = False
    code_lines = []
    table_lines = []
    selected_level_count = 0

    for line_no, raw in enumerate(lines, 1):
        line = raw.rstrip("\r")

        if line.startswith("```"):
            flush_table(sections, table_lines)
            table_lines = []
            if in_code:
                for code_line in code_lines:
                    add_span(sections, "HELP_SPAN_CODE_BLOCK", code_line, line_no)
                code_lines = []
                in_code = False
            else:
                in_code = True
                code_lines = []
            continue

        if in_code:
            code_lines.append(line)
            continue

        stripped = line.lstrip()
        if stripped.startswith("|"):
            table_lines.append((line_no, line))
            continue

        flush_table(sections, table_lines)
        table_lines = []

        heading_match = HEADING_RE.match(line)
        if heading_match:
            level = len(heading_match.group(1))
            title = heading_match.group(2).strip()
            if not title:
                fail(f"empty heading at line {line_no}")

            if level == 1:
                if heading is not None:
                    if sections:
                        add_heading_span(sections, title, line_no)
                    continue
                section_title, heading = split_top_level_title(title)
                if not sections:
                    sections.append({"title": section_title, "spans": []})
            elif level == section_level:
                sections.append({"title": title, "spans": []})
                selected_level_count += 1
            elif sections:
                add_heading_span(sections, title, line_no)
            continue

        if line.strip() == "":
            if sections:
                add_span(sections, "HELP_SPAN_BLANK", "", line_no)
            continue

        if stripped.startswith("- ") or stripped.startswith("* "):
            add_span(sections, "HELP_SPAN_BULLET", stripped[2:].strip(), line_no)
            continue

        match = ORDERED_RE.match(stripped)
        if match:
            add_span(sections, "HELP_SPAN_NUMBER", stripped.strip(), line_no)
            continue

        if stripped.startswith("\\Needspace"):
            continue

        add_span(sections, "HELP_SPAN_TEXT", line.strip(), line_no)

    flush_table(sections, table_lines)
    if in_code:
        fail("unterminated fenced code block")
    if heading is None:
        fail("missing top-level # heading")
    if selected_level_count == 0:
        fail(f"{path}: no level-{section_level} headings found for help sections")
    return heading, sections


def emit(heading, sections):
    out = []
    out.append("/* Generated by tools/gen_help.py. Do not edit. */")
    out.append("")
    out.append("typedef enum help_span_kind {")
    out.append("    HELP_SPAN_TEXT,")
    out.append("    HELP_SPAN_BLANK,")
    out.append("    HELP_SPAN_H3,")
    out.append("    HELP_SPAN_BULLET,")
    out.append("    HELP_SPAN_NUMBER,")
    out.append("    HELP_SPAN_CODE_BLOCK,")
    out.append("    HELP_SPAN_TABLE,")
    out.append("    HELP_SPAN_TABLE_HEADER")
    out.append("} help_span_kind;")
    out.append("")
    out.append("typedef struct help_span {")
    out.append("    help_span_kind kind;")
    out.append("    const char *text;")
    out.append("} help_span;")
    out.append("")
    out.append("typedef struct help_section {")
    out.append("    const char *title;")
    out.append("    const help_span *spans;")
    out.append("    int span_count;")
    out.append("} help_section;")
    out.append("")
    out.append(f"static const char *help_heading = {c_string(heading)};")
    out.append("")
    for index, section in enumerate(sections):
        out.append(f"static const help_span help_section_{index}_spans[] = {{")
        for kind, text in section["spans"]:
            out.append(f"    {{ {kind}, {c_string(text)} }},")
        if not section["spans"]:
            out.append('    { HELP_SPAN_TEXT, "" },')
        out.append("};")
        out.append("")
    out.append("static const help_section help_sections[] = {")
    for index, section in enumerate(sections):
        span_count = len(section["spans"]) if section["spans"] else 1
        out.append(
            f"    {{ {c_string(section['title'])}, help_section_{index}_spans, {span_count} }},")
    out.append("};")
    out.append(f"static const int help_section_count = {len(sections)};")
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--level", type=int, default=2)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    if args.level < 2 or args.level > 6:
        fail("--level must be an integer from 2 to 6")

    heading, sections = parse_manual(args.input, args.level)
    text = emit(heading, sections)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)


if __name__ == "__main__":
    main()
