#!/usr/bin/env python3
"""objectives.py <setup.c> [context-lines]: plan-writing aid for the player bot.

Reads a decompiled stage setup (assets/obseg/setup/Usetup*.c, extracted from your ROM) and prints
  - each objective: number, minimum difficulty (0 agent .. 2 00 agent), and its criteria
    (destroy / collect / photograph tag, complete-on-flag, fail-on-flag, enter room, ...);
  - for every stage flag an objective waits on, the AI-list lines that set it, with the conditions just before
    (if_bond_in_room_with_pad, if_object_was_activated, if_item_is_attached_to_object, ...);
  - the tags (tag id -> setup object index and type).
Flags print as the game's value: the AI macros store them byte-swapped, e.g. objective_bitfield_set_on(0x00100000)
is flag 0x1000. Pad arguments in AI lines are byte-swapped too: if_bond_in_room_with_pad(0x4a01, ...) is pad 0x014a = 330.
"""
import re
import sys

TYPES = {23: 'ObjectiveStart', 24: 'ObjectiveEnd', 25: 'destroy', 26: 'complete-on-flag', 27: 'fail-on-flag',
         28: 'collect', 29: 'deposit', 30: 'photograph', 32: 'enter-room', 33: 'deposit-in-room', 34: 'copy-item'}


def swap32(v):
    return int.from_bytes(v.to_bytes(4, 'little'), 'big')


def swap16(v):
    return ((v & 0xff) << 8) | (v >> 8)


def main():
    path = sys.argv[1]
    ctx = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    src = open(path, encoding='latin-1').read()
    lines = src.split('\n')

    # objectives: '/* Type = Objective...; index = N */' followed by the values line
    print('== objectives ==')
    cur = None
    flags = set()
    for i, ln in enumerate(lines):
        m = re.search(r'/\* Type = (\w+); index = (\d+) \*/', ln)
        if not m:
            continue
        t = m.group(1)
        vals = lines[i + 1].strip().rstrip(',')
        nums = re.findall(r'-?0x[0-9a-fA-F]+|-?\d+', vals.replace('_mkword(0, _mkshort(0, ', ''))
        if t == 'ObjectiveStart':
            # _mkword(0, _mkshort(0, 23)), num, text, difficulty
            parts = [p.strip() for p in vals.split(',')]
            cur = parts[-3] if len(parts) >= 4 else '?'
            print(f'objective {cur}: difficulty {parts[-1]} (0 agent, 1 secret agent, 2 00 agent), text id {parts[-2]}')
        elif t.startswith('Objective') and t != 'ObjectiveEnd' and cur is not None:
            parts = [p.strip() for p in vals.split(',')]
            arg = parts[-1] if parts else '?'
            print(f'   {t:32s} {arg}')
            if t in ('ObjectiveCompleteCondition', 'ObjectiveFailCondition'):
                try:
                    flags.add(int(arg, 0))
                except ValueError:
                    pass
        elif t == 'Tag':
            pass
    print()
    print('== tags (tag id: object at setup index + offset) ==')
    for i, ln in enumerate(lines):
        if '/* Type = Tag;' in ln:
            m = re.search(r'_mkword\((\d+), (0x[0-9a-fA-F]+|\d+)\)', lines[i + 1])
            idx = re.search(r'index = (\d+)', ln).group(1)
            if m:
                off = int(m.group(2), 0)
                if off >= 0x8000:
                    off -= 0x10000
                tgt = int(idx) + off
                tline = next((l for l in lines if f'index = {tgt} */' in l), '')
                tt = re.search(r'Type = (\w+)', tline)
                print(f'tag {m.group(1)}: setup index {tgt} ({tt.group(1) if tt else "?"})')
    print()
    print('== AI lines that set the objective flags ==')
    for f in sorted(flags):
        sw = swap32(f)
        pat = f'objective_bitfield_set_on(0x{sw:08x})'
        print(f'-- flag 0x{f:x} (AI writes {pat})')
        for i, ln in enumerate(lines):
            if pat in ln:
                start = max(0, i - ctx)
                ai = next((lines[j] for j in range(i, -1, -1) if lines[j].startswith('u8 ai_')), '?')
                print(f'   in {ai.strip()} line {i + 1}:')
                for j in range(start, i + 1):
                    s = lines[j].strip()
                    m = re.search(r'if_bond_in_room_with_pad\((0x[0-9a-f]+)', s)
                    if m:
                        s += f'   # pad {swap16(int(m.group(1), 16))}'
                    print(f'      {s}')
        print()


if __name__ == '__main__':
    main()
