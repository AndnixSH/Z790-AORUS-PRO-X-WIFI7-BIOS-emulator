#!/usr/bin/env python3
"""Explain why setup items are shown, hidden or greyed out.

Evaluates the compiled visibility rules exactly like the firmware does,
using the factory defaults plus layout.VOLATILE_PROFILE, and prints every
variable the rule looked at.  Handy when tuning VOLATILE_PROFILE.

  tools/explain.py "CPU Upgrade" "IGP Ratio"
  tools/explain.py --form 0x2711
"""

import argparse
import struct

import hii
import sdb
import uefi_image


def run(expr, comp, trace):
    stack = []
    p = 0
    while True:
        op = expr[p]
        p += 1
        if op == sdb.E_END:
            return stack[-1] if stack else 0
        if op == sdb.E_PUSH:
            stack.append(struct.unpack_from('<Q', expr, p)[0])
            p += 8
        elif op == sdb.E_LOAD:
            var, off, width = struct.unpack_from('<HHB', expr, p)
            p += 5
            vs = comp.varstores[var]
            val = int.from_bytes(vs['data'][off:off + width], 'little')
            trace.append('%s[%#x]=%d' % (vs['name'], off, val))
            stack.append(val)
        elif op == sdb.E_INLIST:
            cnt = struct.unpack_from('<H', expr, p)[0]
            vals = struct.unpack_from('<%dH' % cnt, expr, p + 2)
            p += 2 + 2 * cnt
            stack[-1] = int(stack[-1] in vals)
            trace.append('in %s' % (list(vals),))
        elif op == sdb.E_NOT:
            stack[-1] = int(not stack[-1])
        elif op == sdb.E_BNOT:
            stack[-1] = ~stack[-1] & 0xFFFFFFFFFFFFFFFF
        elif op == sdb.E_COND:
            c, b, a = stack.pop(), stack.pop(), stack[-1]
            stack[-1] = c if a else b
        else:
            b = stack.pop()
            a = stack[-1]
            stack[-1] = {
                sdb.E_EQ: lambda: int(a == b), sdb.E_NE: lambda: int(a != b),
                sdb.E_LT: lambda: int(a < b), sdb.E_LE: lambda: int(a <= b),
                sdb.E_GT: lambda: int(a > b), sdb.E_GE: lambda: int(a >= b),
                sdb.E_AND: lambda: int(bool(a) and bool(b)), sdb.E_OR: lambda: int(bool(a) or bool(b)),
                sdb.E_BAND: lambda: a & b, sdb.E_BOR: lambda: a | b, sdb.E_ADD: lambda: a + b,
                sdb.E_SUB: lambda: a - b, sdb.E_MUL: lambda: a * b,
                sdb.E_DIV: lambda: a // b if b else 0, sdb.E_MOD: lambda: a % b if b else 0,
                sdb.E_SHL: lambda: a << b, sdb.E_SHR: lambda: a >> b,
            }[op]()


def load(bios):
    img = uefi_image.Image(open(bios, 'rb').read())
    setup = max(img.find(name='Setup'), key=lambda f: len(f.body))
    pe = setup.section_data(uefi_image.SECTION_PE32)
    tse = img.find(name='AMITSE')[0].section_data(uefi_image.SECTION_PE32)
    comp = sdb.Compiler(hii.find_form_packages(pe)[0][1], hii.find_string_packages(pe),
                        hii.find_string_packages(tse), uefi_image.find_std_defaults(img), {})
    comp.build_layout()
    comp.annotate()
    return comp


OPNAMES = {sdb.E_EQ: '==', sdb.E_NE: '!=', sdb.E_LT: '<', sdb.E_LE: '<=', sdb.E_GT: '>',
           sdb.E_GE: '>=', sdb.E_AND: 'and', sdb.E_OR: 'or', sdb.E_BAND: '&', sdb.E_BOR: '|',
           sdb.E_ADD: '+', sdb.E_SUB: '-', sdb.E_MUL: '*', sdb.E_DIV: '/', sdb.E_MOD: '%',
           sdb.E_SHL: '<<', sdb.E_SHR: '>>'}


def infix(expr, comp):
    """Decompile the bytecode into a readable expression with live values."""
    stack = []
    p = 0
    while True:
        op = expr[p]
        p += 1
        if op == sdb.E_END:
            return stack[-1] if stack else '?'
        if op == sdb.E_PUSH:
            stack.append(str(struct.unpack_from('<Q', expr, p)[0]))
            p += 8
        elif op == sdb.E_LOAD:
            var, off, width = struct.unpack_from('<HHB', expr, p)
            p += 5
            vs = comp.varstores[var]
            stack.append('%s[%#x]{=%d}' % (vs['name'], off, int.from_bytes(vs['data'][off:off + width], 'little')))
        elif op == sdb.E_INLIST:
            cnt = struct.unpack_from('<H', expr, p)[0]
            vals = struct.unpack_from('<%dH' % cnt, expr, p + 2)
            p += 2 + 2 * cnt
            stack[-1] = '(%s in %s)' % (stack[-1], list(vals))
        elif op == sdb.E_NOT:
            stack[-1] = 'not %s' % stack[-1]
        elif op == sdb.E_BNOT:
            stack[-1] = '~%s' % stack[-1]
        elif op == sdb.E_COND:
            c, b, a = stack.pop(), stack.pop(), stack[-1]
            stack[-1] = '(%s ? %s : %s)' % (a, c, b)
        else:
            b = stack.pop()
            stack[-1] = '(%s %s %s)' % (stack[-1], OPNAMES.get(op, '?'), b)


def explain(comp, s):
    out = []
    for kind, exprs in (('suppress', s.suppress), ('grayout', s.gray)):
        for e in exprs:
            v = run(e, comp, [])
            out.append('   %-8s %s  %s' % (kind, 'TRUE ' if v else 'false', infix(e, comp)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('prompts', nargs='*')
    ap.add_argument('--form', type=lambda x: int(x, 0))
    ap.add_argument('--bios', default='build/bios/bios.bin')
    args = ap.parse_args()
    comp = load(args.bios)
    for fid in comp.form_order:
        f = comp.forms[fid]
        for s in f['stmts']:
            p = comp.text_of(s.prompt)
            if (args.form is not None and fid == args.form) or (p in args.prompts):
                hidden = any(run(e, comp, []) for e in s.suppress)
                gray = any(run(e, comp, []) for e in s.gray)
                print('%-45s form %#06x  %s' % (p or '<%d>' % s.kind, fid,
                                                'HIDDEN' if hidden else ('grey' if gray else 'shown')))
                for line in explain(comp, s):
                    print(line)


if __name__ == '__main__':
    main()
