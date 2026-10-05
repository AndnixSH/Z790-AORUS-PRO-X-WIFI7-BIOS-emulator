"""Compile the BIOS setup forms into the "AORUS setup database" (SDB).

The SDB is a flat little-endian binary that the firmware app reads at
runtime.  It contains the HII strings for every language, the variable
stores (with GIGABYTE's factory defaults), every form/statement/option of
the IFR, the visibility rules compiled to a tiny stack-machine bytecode, and
the GIGABYTE tab layout from layout.py.

The C side of the format lives in firmware/AorusPkg/Include/Sdb.h; keep the
two in sync (SDB_VERSION below).
"""

import re
import struct
import unicodedata
import uuid

import hii
import layout

SDB_VERSION = 1

# Statement kinds (SDB_K_*)
K_SUBTITLE, K_TEXT, K_REF, K_ONEOF, K_CHECKBOX, K_NUMERIC = 1, 2, 3, 4, 5, 6
K_STRING, K_PASSWORD, K_ACTION, K_DATE, K_TIME, K_RESET, K_ORDERED = 7, 8, 9, 10, 11, 12, 13

OP_TO_KIND = {
    hii.OP['SUBTITLE']: K_SUBTITLE, hii.OP['TEXT']: K_TEXT, hii.OP['REF']: K_REF,
    hii.OP['ONE_OF']: K_ONEOF, hii.OP['CHECKBOX']: K_CHECKBOX, hii.OP['NUMERIC']: K_NUMERIC,
    hii.OP['STRING']: K_STRING, hii.OP['PASSWORD']: K_PASSWORD, hii.OP['ACTION']: K_ACTION,
    hii.OP['DATE']: K_DATE, hii.OP['TIME']: K_TIME, hii.OP['RESET_BUTTON']: K_RESET,
    hii.OP['ORDERED_LIST']: K_ORDERED,
}

# Expression bytecode (SDB_E_*)
E_END, E_PUSH, E_LOAD, E_INLIST = 0x00, 0x01, 0x02, 0x03
E_EQ, E_NE, E_LT, E_LE, E_GT, E_GE = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
E_AND, E_OR, E_NOT = 0x20, 0x21, 0x22
E_BAND, E_BOR, E_BNOT, E_ADD, E_SUB, E_MUL, E_DIV, E_MOD, E_SHL, E_SHR = range(0x30, 0x3A)
E_COND = 0x40

BINOPS = {
    hii.OP['EQUAL']: E_EQ, hii.OP['NOT_EQUAL']: E_NE, hii.OP['LESS_THAN']: E_LT,
    hii.OP['LESS_EQUAL']: E_LE, hii.OP['GREATER_THAN']: E_GT, hii.OP['GREATER_EQUAL']: E_GE,
    hii.OP['AND']: E_AND, hii.OP['OR']: E_OR, hii.OP['BITWISE_AND']: E_BAND,
    hii.OP['BITWISE_OR']: E_BOR, hii.OP['ADD']: E_ADD, hii.OP['SUBTRACT']: E_SUB,
    hii.OP['MULTIPLY']: E_MUL, hii.OP['DIVIDE']: E_DIV, hii.OP['MODULO']: E_MOD,
    hii.OP['SHIFT_LEFT']: E_SHL, hii.OP['SHIFT_RIGHT']: E_SHR,
}

# Statements/scopes that end an expression inside a conditional scope.
EXPR_OPS = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x20, 0x21, 0x22, 0x2A, 0x2B, 0x2F, 0x30,
            0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
            0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A,
            0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
            0x58, 0x59, 0x5E, 0x64}
COND_OPS = {hii.OP['SUPPRESS_IF']: 'S', hii.OP['GRAY_OUT_IF']: 'G', hii.OP['DISABLE_IF']: 'S'}

# Numeric display formats (SDB_NF_*)
NF_DEC, NF_HEX, NF_AUTO, NF_AUTO_MV, NF_AUTO_MV_OFS, NF_AUTO_CLK = 0, 1, 2, 3, 4, 5

# Statement flag bits (SDB_SF_*)
SF_HAS_DEFAULT = 0x01
SF_FAVORITE_DEFAULT = 0x02
SF_INDENT = 0x04          # shown indented (prompt starts with spaces)
SF_ORANGE = 0x08          # subtitle shown as orange section title
SF_ACTION = 0x10          # handled by the emulator (see ActionId)
SF_VIRTUAL = 0x20         # synthesized by the layout

DYN_IDS = {}              # name -> id, filled from layout tables
ACTION_IDS = {}
SPECIAL_IDS = {}


def _ids(names, table):
    for n in names:
        if n not in table:
            table[n] = len(table) + 1


_ids(sorted(set(layout.DYNAMIC_TEXT.values()) |
            {e[2] for f in layout.VIRTUAL_FORMS.values() for e in f['entries'] if e[0] == 'text'}),
     DYN_IDS)
_ids(sorted(set(layout.ACTIONS.values()) | {'smartfan', 'qflash', 'boot_override', 'search'}), ACTION_IDS)
_ids(sorted(set(layout.SPECIAL.values())), SPECIAL_IDS)
NAMED_IDS = {}
_ids(list(layout.NAMED_STRINGS), NAMED_IDS)


class CompileError(Exception):
    pass


class Stmt:
    def __init__(self, kind, node=None):
        self.kind = kind
        self.node = node
        self.prompt = self.help = self.text2 = 0
        self.qid = 0
        self.var = 0xFFFF
        self.offset = 0
        self.width = 0
        self.qflags = 0
        self.numflags = 0
        self.min = self.max = self.step = 0
        self.default = 0
        self.flags = 0
        self.options = []          # [(text_id, flags, value, suppress_expr)]
        self.suppress = []         # list of compiled expressions (OR-ed)
        self.gray = []
        self.ref = 0
        self.ref_virtual = None
        self.dyn = 0
        self.action = 0
        self.nfmt = NF_DEC
        self.minlen = self.maxlen = 0
        self.form = 0

    def copy(self):
        s = Stmt(self.kind, self.node)
        s.__dict__.update(self.__dict__)
        s.options = list(self.options)
        s.suppress = list(self.suppress)
        s.gray = list(self.gray)
        return s


class Compiler:
    def __init__(self, form_pkg, string_pkgs, tse_string_pkgs, defaults, bios_info):
        self.tree = hii.parse_ifr(form_pkg)
        self.formset = next(n for n in self.tree if n.op == hii.OP['FORM_SET'])
        self.string_pkgs = string_pkgs
        self.tse_string_pkgs = tse_string_pkgs
        self.defaults = defaults
        self.bios_info = bios_info
        self.en = next(s for _, lang, s, _ in string_pkgs if lang == 'en-US')
        self.tse_en = next((s for _, lang, s, _ in tse_string_pkgs if lang == 'en-US'), {})
        self.extra_strings = {}            # text -> id (0x6000+)
        self.varstores = []                # dicts
        self.var_by_id = {}
        self.questions = {}                # qid -> (varindex, offset, width)
        self.forms = {}                    # form id -> {'title', 'stmts'}
        self.form_order = []
        self.warnings = []
        self._collect_varstores()
        self._collect_questions()
        self._collect_forms()

    # -- strings ---------------------------------------------------------
    def string_id(self, text, create=True):
        for sid, s in self.en.items():
            if s == text:
                return sid
        if not create:
            return 0
        if text not in self.extra_strings:
            self.extra_strings[text] = 0x6000 + len(self.extra_strings)
        return self.extra_strings[text]

    def named_string(self, spec):
        prefix = isinstance(spec, tuple)
        text = spec[1] if prefix else spec
        for table, base in ((self.en, 0), (self.tse_en, 0x4000)):
            for sid in sorted(table):
                t = table[sid]
                if (t.startswith(text) if prefix else t == text):
                    return base + sid
        return self.string_id(text)

    def text_of(self, sid):
        if sid >= 0x6000:
            for t, i in self.extra_strings.items():
                if i == sid:
                    return t
        return self.en.get(sid, '')

    # -- varstores --------------------------------------------------------
    def _collect_varstores(self):
        for n in self.formset.walk():
            if n.op == hii.OP['VARSTORE']:
                vid, size = n.u16(16), n.u16(18)
                name = n.body[20:].split(b'\0')[0].decode('ascii')
                guid = n.guid(0)
                attrs = 0x7
            elif n.op == hii.OP['VARSTORE_EFI']:
                vid = n.u16(0)
                guid = n.guid(2)
                attrs, size = n.u32(18), n.u16(22)
                name = n.body[24:].split(b'\0')[0].decode('ascii')
            else:
                continue
            if vid in self.var_by_id:
                continue
            key = (name, str(guid).upper())
            data = bytearray(size)
            flags = 0
            if key in self.defaults:
                d = self.defaults[key]
                data[:min(size, len(d))] = d[:size]
                flags |= 1                         # has factory defaults
            prof = layout.VOLATILE_PROFILE.get(name)
            if prof:
                for off, (val, width) in prof.items():
                    if off == '*':             # fill the whole store
                        data[:] = bytes([val]) * size
                    else:
                        data[off:off + width] = val.to_bytes(width, 'little')
            if key not in self.defaults:
                flags |= 2                         # volatile: not saved
            vs = dict(id=vid, name=name, guid=guid, size=size, data=bytes(data),
                      flags=flags, attrs=attrs, index=len(self.varstores))
            self.var_by_id[vid] = vs
            self.varstores.append(vs)

    # -- questions -------------------------------------------------------
    @staticmethod
    def _width(n):
        if n.op in (hii.OP['ONE_OF'], hii.OP['NUMERIC']):
            return 1 << (n.u8(11) & 3)
        if n.op == hii.OP['CHECKBOX']:
            return 1
        return 0

    def _collect_questions(self):
        for n in self.formset.walk():
            if n.op in hii.QUESTION_OPS:
                qid, vid, off = n.u16(4), n.u16(6), n.u16(8)
                w = self._width(n)
                if vid in self.var_by_id and w:
                    self.questions.setdefault(qid, (self.var_by_id[vid]['index'], off, w))

    # -- expressions -----------------------------------------------------
    def compile_expr(self, ops):
        out = bytearray()
        depth = 0

        def load(qid):
            q = self.questions.get(qid)
            if q is None:
                out.extend(struct.pack('<BQ', E_PUSH, 0))
            else:
                out.extend(struct.pack('<BHHB', E_LOAD, q[0], q[1], q[2]))

        # AMI's compiler sometimes sets the scope bit on the first opcode of
        # an expression, nesting the rest under it; binary order is a
        # pre-order walk, so flatten before compiling.
        flat = [x for n in ops for x in n.walk()]
        for n in flat:
            op = n.op
            if op == hii.OP['EQ_ID_VAL']:
                load(n.u16(0)); out.extend(struct.pack('<BQ', E_PUSH, n.u16(2))); out.append(E_EQ); depth += 1
            elif op == hii.OP['EQ_ID_ID']:
                load(n.u16(0)); load(n.u16(2)); out.append(E_EQ); depth += 1
            elif op == hii.OP['EQ_ID_VAL_LIST']:
                qid, cnt = n.u16(0), n.u16(2)
                vals = [n.u16(4 + 2 * i) for i in range(cnt)]
                load(qid)
                out.extend(struct.pack('<BH', E_INLIST, cnt))
                for v in vals:
                    out.extend(struct.pack('<H', v))
                depth += 1
            elif op == hii.OP['QUESTION_REF1']:
                load(n.u16(0)); depth += 1
            elif op == hii.OP['GET']:
                vid, off, vtype = n.u16(0), n.u16(2), n.u8(4)
                vs = self.var_by_id.get(vid)
                if vs is None:
                    out.extend(struct.pack('<BQ', E_PUSH, 0))
                else:
                    out.extend(struct.pack('<BHHB', E_LOAD, vs['index'], off, 1 << (vtype & 3)))
                depth += 1
            elif op in (hii.OP['UINT8'], hii.OP['UINT16'], hii.OP['UINT32'], hii.OP['UINT64']):
                size = {0x42: 1, 0x43: 2, 0x44: 4, 0x45: 8}[op]
                out.extend(struct.pack('<BQ', E_PUSH, int.from_bytes(n.body[:size], 'little'))); depth += 1
            elif op in (hii.OP['TRUE'], hii.OP['ONE']):
                out.extend(struct.pack('<BQ', E_PUSH, 1)); depth += 1
            elif op in (hii.OP['FALSE'], hii.OP['ZERO'], hii.OP['UNDEFINED']):
                out.extend(struct.pack('<BQ', E_PUSH, 0)); depth += 1
            elif op == hii.OP['ONES']:
                out.extend(struct.pack('<BQ', E_PUSH, 0xFFFFFFFFFFFFFFFF)); depth += 1
            elif op == hii.OP['NOT']:
                out.append(E_NOT)
            elif op == hii.OP['BITWISE_NOT']:
                out.append(E_BNOT)
            elif op in BINOPS:
                out.append(BINOPS[op]); depth -= 1
            elif op == hii.OP['CONDITIONAL']:
                out.append(E_COND); depth -= 2
            elif op == hii.OP['STRING_REF1']:
                out.extend(struct.pack('<BQ', E_PUSH, 0)); depth += 1
            elif op == hii.OP['MATCH']:
                out.append(E_EQ); depth -= 1          # string compare: never matches
            else:
                raise CompileError('unsupported expression opcode %s' % n.name)
        if depth != 1:
            raise CompileError('unbalanced expression (depth %d)' % depth)
        out.append(E_END)
        return bytes(out)

    def _split_condition(self, node):
        """Split a conditional scope into (expression ops, body nodes)."""
        i = 0
        while i < len(node.children) and node.children[i].op in EXPR_OPS:
            i += 1
        return node.children[:i], node.children[i:]

    # -- statements -------------------------------------------------------
    def _make_stmt(self, n, conds, form):
        kind = OP_TO_KIND[n.op]
        s = Stmt(kind, n)
        s.form = form
        s.prompt, s.help = n.u16(0), n.u16(2)
        for c, expr in conds:
            (s.suppress if c == 'S' else s.gray).append(expr)
        if kind == K_SUBTITLE:
            return s
        if kind == K_TEXT:
            s.text2 = n.u16(4)
            return s
        if kind == K_RESET:
            return s
        s.qid, vid, s.offset, s.qflags = n.u16(4), n.u16(6), n.u16(8), n.u8(10)
        if vid in self.var_by_id:
            s.var = self.var_by_id[vid]['index']
        s.width = self._width(n)
        if kind in (K_ONEOF, K_NUMERIC):
            s.numflags = n.u8(11)
            sz = 1 << (s.numflags & 3)
            fmt = {1: 'B', 2: 'H', 4: 'I', 8: 'Q'}[sz]
            s.min, s.max, s.step = struct.unpack_from('<3' + fmt, n.body, 12)
        elif kind == K_CHECKBOX:
            s.numflags = n.u8(11)
            if s.numflags & 0x01:
                s.default = 1
                s.flags |= SF_HAS_DEFAULT
        elif kind == K_REF:
            s.ref = n.u16(11) if len(n.body) >= 13 else 0
        elif kind == K_STRING:
            s.min, s.max = s.minlen, s.maxlen = n.u8(11), n.u8(12)
        elif kind == K_PASSWORD:
            s.min, s.max = s.minlen, s.maxlen = n.u16(11), n.u16(13)
        elif kind in (K_DATE, K_TIME):
            s.numflags = n.u8(11)
        # options, defaults and option-level conditions live inside the scope
        self._scan_question_scope(s, n.children, [])
        if kind == K_NUMERIC:
            s.nfmt = self._numeric_format(s)
        return s

    def _scan_question_scope(self, s, children, conds):
        for c in children:
            if c.op == hii.OP['ONE_OF_OPTION']:
                text, oflags, otype = c.u16(0), c.u8(2), c.u8(3)
                size = {0: 1, 1: 2, 2: 4, 3: 8, 4: 1}.get(otype, 8)
                val = int.from_bytes(c.body[4:4 + size], 'little')
                override = layout.OPTION_TEXT.get((self.text_of(s.prompt), val))
                if override:
                    text = self.string_id(override)
                sup = self._or_exprs([e for k, e in conds if k == 'S'])
                s.options.append((text, oflags, val, sup))
                if oflags & 0x10 and not s.flags & SF_HAS_DEFAULT:   # EFI_IFR_OPTION_DEFAULT
                    s.default = val
                    s.flags |= SF_HAS_DEFAULT
            elif c.op == hii.OP['DEFAULT'] and not c.children:
                did, dtype = c.u16(0), c.u8(2)
                if did == 0 and dtype <= 4:
                    size = {0: 1, 1: 2, 2: 4, 3: 8, 4: 1}[dtype]
                    s.default = int.from_bytes(c.body[3:3 + size], 'little')
                    s.flags |= SF_HAS_DEFAULT
            elif c.op in COND_OPS:
                ops, body = self._split_condition(c)
                try:
                    expr = self.compile_expr(ops)
                except CompileError as e:
                    self.warnings.append('option condition: %s' % e)
                    expr = None
                self._scan_question_scope(s, body, conds + ([(COND_OPS[c.op], expr)] if expr else []))

    def _or_exprs(self, exprs):
        if not exprs:
            return None
        out = bytearray(exprs[0][:-1])
        for e in exprs[1:]:
            out += e[:-1]
            out.append(E_OR)
        out.append(E_END)
        return bytes(out)

    def _numeric_format(self, s):
        if s.numflags & 0x30 == 0x10 or s.numflags & 0x30 == 0x20:
            if s.numflags & 0x30 == 0x20:
                return NF_HEX
        if not (s.min == 0 and s.max in (0xFFFF, 0xFFFFFFFF) and s.default == 0):
            return NF_DEC
        prompt = self.text_of(s.prompt)
        if re.search(r'Base Clock|Host Clock|BCLK Freq', prompt):
            return NF_AUTO_CLK
        if re.search(r'Offset|DVID', prompt) and re.search(r'Vcore|Volt|VCC|VDD|VAXG|RING|L2Atom|DVID', prompt):
            return NF_AUTO_MV_OFS
        if re.search(r'Vcore|Voltage|VCC|VDD|V1P8|VOP|VAXG|VNN|L2Atom|VRIN|VPP|PLL', prompt):
            return NF_AUTO_MV
        return NF_AUTO

    def _walk_form(self, nodes, conds, out, form):
        for n in nodes:
            if n.op in COND_OPS:
                ops, body = self._split_condition(n)
                try:
                    expr = self.compile_expr(ops)
                    c2 = conds + [(COND_OPS[n.op], expr)]
                except CompileError as e:
                    self.warnings.append('form %#x: %s' % (form, e))
                    c2 = conds
                self._walk_form(body, c2, out, form)
            elif n.op in OP_TO_KIND:
                out.append(self._make_stmt(n, conds, form))
            elif n.op == hii.OP['GUID'] or n.op in EXPR_OPS:
                pass

    def _collect_forms(self):
        for n in self.formset.children:
            if n.op == hii.OP['FORM']:
                fid, title = n.u16(0), n.u16(2)
                stmts = []
                self._walk_form(n.children, [], stmts, fid)
                self.forms[fid] = dict(title=title, stmts=stmts, virtual=False)
                self.form_order.append(fid)

    # -- layout ----------------------------------------------------------
    def build_layout(self):
        used_virtual_ids = {}
        next_id = 0xF000
        for name in layout.VIRTUAL_FORMS:
            used_virtual_ids[name] = next_id
            next_id += 1
        used_virtual_ids['favorites'] = 0xFFF0
        self.virtual_ids = used_virtual_ids

        def resolve_target(t):
            if isinstance(t, int):
                return t, 0
            if t.startswith('@'):
                return 0, ACTION_IDS[t[1:]]
            return used_virtual_ids[t], 0

        for name, spec in layout.VIRTUAL_FORMS.items():
            fid = used_virtual_ids[name]
            stmts = []
            used = set()
            search = spec.get('search', [])
            for e in spec['entries']:
                kind = e[0]
                if kind == 'items':
                    found = False
                    for f in search:
                        matches = [s for s in self.forms[f]['stmts'] if self.text_of(s.prompt) == e[1]]
                        if matches:
                            for m in matches:
                                stmts.append(m.copy())
                                used.add(id(m))
                            found = True
                            break
                    if not found:
                        self.warnings.append('layout %s: item %r not found' % (name, e[1]))
                elif kind == 'ref':
                    s = Stmt(K_REF)
                    s.prompt = self.string_id(e[1])
                    s.ref, s.action = resolve_target(e[2])
                    s.flags |= SF_VIRTUAL | (SF_ACTION if s.action else 0)
                    stmts.append(s)
                elif kind == 'action':
                    s = Stmt(K_ACTION)
                    s.prompt = self.named_string(e[1])
                    s.action = ACTION_IDS[e[2]]
                    s.flags |= SF_VIRTUAL | SF_ACTION
                    stmts.append(s)
                elif kind == 'subtitle':
                    s = Stmt(K_SUBTITLE)
                    s.prompt = self.string_id(e[1])
                    s.flags |= SF_VIRTUAL | SF_ORANGE
                    stmts.append(s)
                elif kind == 'text':
                    s = Stmt(K_TEXT)
                    s.prompt = self.string_id(e[1])
                    s.dyn = DYN_IDS[e[2]]
                    s.flags |= SF_VIRTUAL
                    stmts.append(s)
                elif kind == 'blank':
                    s = Stmt(K_SUBTITLE)
                    s.flags |= SF_VIRTUAL
                    stmts.append(s)
                elif kind == 'form':
                    stmts.extend(x.copy() for x in self.forms[e[1]]['stmts'])
                elif kind == 'rest':
                    excl = set(e[2])
                    io_used = set()
                    for other in layout.VIRTUAL_FORMS.values():
                        for oe in other['entries']:
                            if oe[0] == 'items':
                                io_used.add(oe[1])
                    for f in e[1]:
                        for s in self.forms[f]['stmts']:
                            p = self.text_of(s.prompt)
                            if p in excl or p in io_used:
                                continue
                            if s.kind == K_SUBTITLE and not p:
                                continue
                            stmts.append(s.copy())
            self.forms[fid] = dict(title=self.string_id(spec['title']), stmts=stmts, virtual=True)
            self.form_order.append(fid)

        # Dynamic favourites page: header subtitle from the real form
        fav = self.forms[layout.F_FAVORITES]
        self.forms[0xFFF0] = dict(title=fav['title'], stmts=[x.copy() for x in fav['stmts']], virtual=True)
        self.form_order.append(0xFFF0)

        self.tabs = []
        for t in layout.TABS:
            fid = t['form'] if isinstance(t['form'], int) else used_virtual_ids[t['form']]
            self.tabs.append((self.string_id(t['title']), fid, t['icon']))

    def annotate(self):
        favs = set(layout.DEFAULT_FAVORITES)
        marked = set()
        for fid in layout.TWEAKER_SEARCH + [f for f in self.form_order if f not in layout.TWEAKER_SEARCH]:
            for s in self.forms[fid]['stmts']:
                p = self.text_of(s.prompt)
                if p in favs and p not in marked and s.kind in (K_ONEOF, K_NUMERIC, K_CHECKBOX):
                    s.flags |= SF_FAVORITE_DEFAULT       # one question per item
                    marked.add(p)
        for f in self.forms.values():
            for s in f['stmts']:
                p = self.text_of(s.prompt)
                if p.startswith('  '):
                    s.flags |= SF_INDENT
                if s.kind == K_TEXT and not s.dyn and p in layout.DYNAMIC_TEXT:
                    s.dyn = DYN_IDS[layout.DYNAMIC_TEXT[p]]
                if s.kind in (K_REF, K_ACTION, K_TEXT) and p in layout.ACTIONS:
                    s.action = ACTION_IDS[layout.ACTIONS[p]]
                    s.flags |= SF_ACTION
        # Save & Exit: AMI implements these entries in AMITSE; they sit behind
        # conditions that only make sense with AMITSE's private variables.
        override = False
        for s in self.forms[layout.F_EXIT]['stmts']:
            p = self.text_of(s.prompt)
            if s.kind == K_SUBTITLE and p == 'Boot Override':
                override = True
            elif override and s.kind == K_REF and not p:
                # AMITSE fills this with one entry per boot device
                s.action = ACTION_IDS['boot_override']
                s.flags |= SF_ACTION
                override = False
            if s.flags & SF_ACTION:
                s.suppress = []

    def specials(self):
        res = []
        for prompt, key in layout.SPECIAL.items():
            pref = layout.SPECIAL_FORM.get(prompt)
            order = ([pref] if pref else []) + [f for f in self.form_order if f != pref]
            for fid in order:
                f = self.forms[fid]
                if f['virtual']:
                    continue
                hit = next((s for s in f['stmts'] if self.text_of(s.prompt) == prompt), None)
                if hit is not None:
                    res.append((SPECIAL_IDS[key], hit))
                    break
            else:
                self.warnings.append('special question %r not found' % prompt)
        return res

    # -- serialization ---------------------------------------------------
    def serialize(self):
        self.build_layout()
        self.annotate()
        specials = self.specials()

        expr_blob = bytearray(b'\0' * 4)
        expr_cache = {}

        def put_expr(exprs):
            e = self._or_exprs([x for x in exprs if x])
            if e is None:
                return 0xFFFFFFFF
            if e not in expr_cache:
                expr_cache[e] = len(expr_blob)
                expr_blob.extend(e)
            return expr_cache[e]

        stmt_blob = bytearray()
        opt_blob = bytearray()
        form_blob = bytearray()
        stmt_index = {}
        nstmt = 0
        nopt = 0
        for fid in self.form_order:
            f = self.forms[fid]
            first = nstmt
            for s in f['stmts']:
                optfirst = nopt
                for text, oflags, val, sup in s.options:
                    opt_blob += struct.pack('<QHBBI', val, text, oflags, 0,
                                            put_expr([sup]) if sup else 0xFFFFFFFF)
                    nopt += 1
                stmt_index[id(s)] = nstmt
                stmt_blob += struct.pack(
                    '<QQQQ' 'IIII' 'HHHHHHHH' 'BBBBBBBB',
                    s.min, s.max, s.step, s.default,
                    optfirst, put_expr(s.suppress), put_expr(s.gray), 0,
                    s.prompt, s.help, s.text2, s.qid,
                    s.var, s.offset, s.ref, len(s.options),
                    s.kind, s.qflags, s.width, s.numflags,
                    s.dyn, s.flags, s.action, s.nfmt)
                nstmt += 1
            form_blob += struct.pack('<HHII', fid, f['title'], first, nstmt - first)

        var_blob = bytearray()
        data_blob = bytearray()
        for vs in self.varstores:
            name16 = vs['name'].encode('utf-16le')[:78].ljust(80, b'\0')
            var_blob += struct.pack('<HHI', vs['id'], vs['flags'], vs['size'])
            var_blob += vs['guid'].bytes_le + name16
            var_blob += struct.pack('<II', len(data_blob), vs['attrs'])
            data_blob += vs['data']
            while len(data_blob) % 8:
                data_blob += b'\0'

        for name in layout.NAMED_STRINGS:
            self.named_string(layout.NAMED_STRINGS[name])

        # Strings: setup strings (1..), AMITSE strings (+0x4000), extras (0x6000+)
        langs = []
        for _, lang, strings, pkg in self.string_pkgs:
            merged = dict(strings)
            name_id = struct.unpack_from('<H', pkg, 0x2C)[0]
            for _, tl, ts, _ in self.tse_string_pkgs:
                if tl == lang:
                    for k, v in ts.items():
                        merged[0x4000 + k] = v
            if lang == 'en-US':
                for t, i in self.extra_strings.items():
                    merged[i] = t
            langs.append((lang, strings.get(name_id, lang), merged))
        str_blob = bytearray()
        lang_hdr = bytearray()
        hdr_size = 32 * len(langs)
        tables = bytearray()
        for lang, native, merged in langs:
            maxid = max(merged) if merged else 0
            offs = [0] * (maxid + 1)
            for sid, text in merged.items():
                offs[sid] = 1 + len(str_blob)      # +1 so that 0 means "absent"
                text = unicodedata.normalize('NFC', text)
                str_blob += text.encode('utf-16le') + b'\0\0'
            lang_hdr += struct.pack('<8sII', lang.encode('ascii'), maxid, hdr_size + len(tables))
            lang_hdr += native.encode('utf-16le')[:14].ljust(16, b'\0')
            tables += struct.pack('<%dI' % len(offs), *offs)
        strs_section = bytes(lang_hdr) + bytes(tables)

        tab_blob = bytearray()
        icon_ids = {'star': 1, 'gauge': 2, 'gear': 3, 'info': 4, 'power': 5, 'exit': 6}
        for title, fid, icon in self.tabs:
            tab_blob += struct.pack('<HHHH', title, fid, icon_ids[icon], 0)

        nstr_blob = bytearray()
        for name in layout.NAMED_STRINGS:
            nstr_blob += struct.pack('<H', self.named_string(layout.NAMED_STRINGS[name]))

        spec_blob = bytearray()
        for key, s in specials:
            spec_blob += struct.pack('<HHI', key, 0, stmt_index[id(s)])

        info = self.bios_info
        info_blob = bytearray()
        for k in ('model', 'bios_version', 'bios_date', 'bios_id', 'board_short'):
            info_blob += info.get(k, '').encode('utf-16le')[:62].ljust(64, b'\0')

        sections = [
            (b'INFO', bytes(info_blob), 1),
            (b'LANG', bytes(strs_section), len(langs)),
            (b'STRD', bytes(str_blob), 0),
            (b'VARS', bytes(var_blob), len(self.varstores)),
            (b'VDAT', bytes(data_blob), 0),
            (b'FORM', bytes(form_blob), len(self.form_order)),
            (b'STMT', bytes(stmt_blob), nstmt),
            (b'OPTS', bytes(opt_blob), nopt),
            (b'EXPR', bytes(expr_blob), 0),
            (b'TABS', bytes(tab_blob), len(self.tabs)),
            (b'SPEC', bytes(spec_blob), len(specials)),
            (b'NSTR', bytes(nstr_blob), len(layout.NAMED_STRINGS)),
        ]
        header_size = 24 + 16 * len(sections)
        out = bytearray()
        body = bytearray()
        table = bytearray()
        for tag, data, count in sections:
            while (header_size + len(body)) % 8:
                body += b'\0'
            table += struct.pack('<4sIII', tag, header_size + len(body), len(data), count)
            body += data
        out += struct.pack('<8sIIII', b'AORUSSDB', SDB_VERSION, header_size + len(body),
                           len(sections), 0)
        out += table + body
        return bytes(out)


def c_header_constants():
    """Constants shared with the C side (written into SdbIds.h)."""
    lines = ['/* Generated by tools/sdb.py - do not edit. */', '#ifndef SDB_IDS_H_', '#define SDB_IDS_H_', '']
    for prefix, table in (('SDB_DYN_', DYN_IDS), ('SDB_ACT_', ACTION_IDS), ('SDB_SPEC_', SPECIAL_IDS),
                          ('SDB_STR_', {k: v - 1 for k, v in NAMED_IDS.items()})):
        for name, val in sorted(table.items(), key=lambda x: x[1]):
            lines.append('#define %s%-24s %d' % (prefix, name.upper(), val))
        lines.append('#define %sCOUNT %d' % (prefix, len(table) + 1))
        lines.append('')
    lines.append('#endif')
    return '\n'.join(lines) + '\n'
