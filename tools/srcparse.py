#!/usr/bin/env python3
"""Parse GenAm 68k source into (label, token, line) items for alignment against the binary.
Conditional assembly is evaluated against DEFS (the AA Toasterless build); MACRO bodies are skipped;
unknown macros become a '?' wildcard token."""
import re, sys

DEFS = {'AACHIPS', 'TOASTERLESS', 'V20', 'TB_DEBUG', 'RELEASE', 'FINAL_CODE'}
OPS68 = set('''abcd add adda addi addq addx and andi asl asr bcc bcs beq bge bgt bhi ble bls blt bmi bne bpl bvc bvs bra bsr bhs blo
bchg bclr bset btst chk clr cmp cmpa cmpi cmpm cmp2 dbcc dbcs dbeq dbf dbra dbge dbgt dbhi dble dbls dblt dbmi dbne dbpl dbt dbvc dbvs
divs divu divsl divul eor eori exg ext extb illegal jmp jsr lea link lsl lsr move movea movem movep moveq muls mulu nbcd neg negx nop
not or ori pea reset rol ror roxl roxr rte rtr rts sbcc scc scs seq sf sge sgt shi sle sls slt smi sne spl st svc svs sub suba subi
subq subx swap tas trap trapv tst unlk bfextu bfexts bfins bfset bfclr bftst bfffo bfchg rtd movec moves cas cas2 pack unpk
fmove fadd fsub fmul fdiv fcmp ftst fbeq fbne'''.split())
MACRO_TOK = {'CALL': 'call', 'XJSR': 'call', 'XSYS': 'call', 'CALLTL': 'call', 'CALLROM': 'call', 'XCALL': 'call', 'CALLEF': 'call',
             'JSR': 'call', 'PUT': 'move', 'GET': 'move', 'XLEA': 'lea', 'XJMP': 'jmp', 'XBSR': 'call', 'LINKSYS': 'call',
             'CALLSYS': 'call', 'JMPTL': 'jmp', 'DEA': 'lea', 'SETAL': 'move'}
DEBUG_MACROS = {'DUMPMSG', 'DUMPREG', 'DUMPMEM', 'DUMPHEXI', 'DUMPUDEC', 'DUMPSDEC', 'DUMPFGCNAME', 'DUMPSTR', 'DUMPTXT', 'WAIT4LMB',
                'STEPMSG', 'DEBUGMSG', 'DUMPBITS', 'DUMPRANGE', 'DUMPHEXB', 'DUMPHEXW', 'DUMPHEXL', 'DUMPFGC'}
DIRECTIVES = {'include', 'incbin', 'xdef', 'xref', 'section', 'even', 'cnop', 'dc', 'ds', 'dcb', 'equ', 'set', 'equr', 'reg', 'macro',
              'endm', 'end', 'ifd', 'ifnd', 'ifeq', 'ifne', 'ifc', 'ifnc', 'ifgt', 'iflt', 'ifge', 'ifle', 'else', 'endc', 'endif', 'opt',
              'xcode', 'xdata', 'xref.l', 'list', 'nolist', 'ttl', 'idnt', 'rsreset', 'rs', 'rsset', 'printx', 'fail', 'csect', 'code',
              'data', 'bss', 'output', 'incdir', 'mexit', 'rept', 'endr', 'align', 'public', 'far', 'near', 'machine', 'mc68020', 'mc68000',
              'xsym', 'xsec', 'org', 'offset', 'struct', 'structure', 'byte', 'word', 'long', 'aptr', 'bptr', 'label', 'ulong', 'uword',
              'ubyte', 'apt', 'bool', 'cptr', 'short', 'ushort', 'float', 'double', 'rptr', 'sectn', 'ifeq.l'}

def norm(op):
    op = op.lower()
    base = op.split('.')[0]
    if base in ('movea',): base = 'move'
    if base in ('adda', 'addi', 'addq'): base = 'add'
    if base in ('suba', 'subi', 'subq'): base = 'sub'
    if base in ('cmpa', 'cmpi', 'cmpm'): base = 'cmp'
    if base in ('andi',): base = 'and'
    if base in ('ori',): base = 'or'
    if base in ('eori',): base = 'eor'
    if base in ('jsr', 'bsr'): base = 'call'
    if base in ('bhs',): base = 'bcc'
    if base in ('blo',): base = 'bcs'
    if base in ('dbra',): base = 'dbf'
    if base == 'moveq' : base = 'move'
    return base

def parse(path, defs=DEFS):
    items = []          # (label or None, token, lineno, text)
    stack = []          # condition stack of bools
    inmacro = False
    pending = []
    for no, raw in enumerate(open(path, encoding='latin-1'), 1):
        line = raw.rstrip('\r\n')
        s = line.split(';')[0]
        if not s.strip() or s.lstrip().startswith('*') and (s.startswith('*') or not s[0].isspace()):
            if s.startswith('*') or not s.strip():
                continue
        m = re.match(r'^([A-Za-z_.][\w.$]*:?|\d+\$:?)?\s*(\S*)\s*(.*)$', s)
        label, op, rest = m.group(1), m.group(2), m.group(3)
        if label:
            label = label.rstrip(':')
        opl = op.lower()
        if label and label.upper() == label and opl in ('macro',) or opl == 'macro':
            inmacro = True; continue
        if inmacro:
            if opl in ('endm',): inmacro = False
            continue
        active = all(stack)
        if opl in ('ifd', 'ifnd'):
            sym = rest.split()[0] if rest.split() else ''
            v = sym in defs
            stack.append(v if opl == 'ifd' else not v); continue
        if opl.startswith('if'):
            stack.append(True); continue      # unknown expression: keep
        if opl == 'else':
            if stack: stack[-1] = not stack[-1]
            continue
        if opl in ('endc', 'endif'):
            if stack: stack.pop()
            continue
        if not active:
            continue
        if opl in ('set', 'equ') and label:
            if opl == 'set': defs = defs | {label}
            continue
        if label and not (opl.split('.')[0] in DIRECTIVES and opl.split('.')[0] in ('equ', 'set', 'equr', 'reg', 'rs', 'macro')):
            pending.append(label)
        if not op:
            continue
        b = opl.split('.')[0]
        if b in DIRECTIVES:
            if b in ('dc', 'ds', 'dcb'):
                items.append((pending, 'DATA', no, line.strip())); pending = []
            continue
        up = op.split('.')[0].upper()
        if up in DEBUG_MACROS:
            continue
        if up in MACRO_TOK:
            tok = MACRO_TOK[up]
        elif b in OPS68 or b in ('movea', 'moveq') or re.match(r'^(b|db|s|trap)(cc|cs|eq|ne|ge|gt|hi|le|ls|lt|mi|pl|vc|vs|ra|sr|t|f|hs|lo)$', b):
            tok = norm(op)
        else:
            tok = '?'
        items.append((pending, tok, no, line.strip())); pending = []
    return items

if __name__ == '__main__':
    it = parse(sys.argv[1])
    from collections import Counter
    print(len(it), Counter(t for _, t, _, _ in it).most_common(12))
    print(Counter(l.split()[0] for _, t, _, l in it if t == '?').most_common(30))
