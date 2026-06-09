#!/usr/bin/env python3
"""Box-aware misalignment detector.
For each ┌──...──┐ top, traces matching │...│ lines down to └──...──┘ at same visual cols."""
import unicodedata
from pathlib import Path

def vwidth(s):
    w = 0
    for c in s:
        eaw = unicodedata.east_asian_width(c)
        if eaw in ('W', 'F'):
            w += 2
        elif unicodedata.category(c) in ('Mn', 'Cc', 'Cf'):
            w += 0
        else:
            w += 1
    return w

def visual_col_of_char(line, char_idx):
    return vwidth(line[:char_idx])

def find_box_tops(line):
    boxes = []
    i = 0
    while i < len(line):
        if line[i] == '┌':
            j = i + 1
            while j < len(line) and line[j] in '─┬':
                j += 1
            if j < len(line) and line[j] == '┐':
                boxes.append((i, j, visual_col_of_char(line, i), visual_col_of_char(line, j)))
            i = j + 1
        else:
            i += 1
    return boxes

def char_at_vcol(line, vcol):
    w = 0
    for i, c in enumerate(line):
        if w == vcol:
            return i
        w += vwidth(c)
    return None

def check_md(path):
    text = path.read_text()
    lines = text.split('\n')
    in_block = False
    block_start = 0
    block_lines = []
    all_issues = []
    for i, ln in enumerate(lines):
        if ln.startswith('```'):
            if in_block:
                issues = check_block(block_lines, block_start)
                if issues:
                    all_issues.append((block_start, issues))
                in_block = False
                block_lines = []
            else:
                in_block = True
                block_start = i + 2
        elif in_block:
            block_lines.append(ln)
    return all_issues

def check_block(blines, base_line):
    issues = []
    for top_idx, ln in enumerate(blines):
        for (s_idx, e_idx, s_vcol, e_vcol) in find_box_tops(ln):
            for bot_idx in range(top_idx + 1, len(blines)):
                bln = blines[bot_idx]
                pos = char_at_vcol(bln, s_vcol)
                if pos is not None and pos < len(bln) and bln[pos] == '└':
                    end_pos = char_at_vcol(bln, e_vcol)
                    if end_pos is None or end_pos >= len(bln) or bln[end_pos] != '┘':
                        actual_right = None
                        for k in range(pos+1, len(bln)):
                            if bln[k] == '┘':
                                actual_right = visual_col_of_char(bln, k)
                                break
                        issues.append((base_line + bot_idx, f'bottom: ┘ expected at vcol {e_vcol}, found at {actual_right}'))
                        break
                    for mid_idx in range(top_idx + 1, bot_idx):
                        mln = blines[mid_idx]
                        left_pos = char_at_vcol(mln, s_vcol)
                        right_pos = char_at_vcol(mln, e_vcol)
                        left_ok = left_pos is not None and left_pos < len(mln) and mln[left_pos] in '│├╞┼'
                        right_ok = right_pos is not None and right_pos < len(mln) and mln[right_pos] in '│┤╡┼'
                        if not right_ok:
                            actual = None
                            if left_ok:
                                for k in range(left_pos+1, len(mln)):
                                    if mln[k] in '│┤':
                                        actual = visual_col_of_char(mln, k)
                                        break
                            issues.append((base_line + mid_idx, f'right │ at vcol {e_vcol} missing; actual {actual}'))
                    break
                if pos is not None and pos < len(bln) and bln[pos] == '┌':
                    break
    return issues

def main():
    root = Path('sections')
    total_files = 0
    files_with_issues = 0
    total_issues = 0
    for md in sorted(root.rglob('*.md')):
        if not any(c in md.read_text() for c in '┌└│'):
            continue
        total_files += 1
        issues = check_md(md)
        if issues:
            files_with_issues += 1
            total_issues += sum(len(i[1]) for i in issues)
            print(f'\n=== {md} ===')
            for block_line, ilist in issues:
                print(f'  block @ L{block_line}: {len(ilist)} issue(s)')
                for line_no, msg in ilist[:8]:
                    print(f'    L{line_no}: {msg}')
    print(f'\nSummary: {total_issues} issues in {files_with_issues}/{total_files} files')

if __name__ == '__main__':
    main()
