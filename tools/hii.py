"""UEFI HII package parsing: string packages and IFR form packages."""

import struct
import uuid

# ---------------------------------------------------------------------------
# Locating packages inside a PE image
# ---------------------------------------------------------------------------

HII_PACKAGE_FORMS = 0x02
HII_PACKAGE_STRINGS = 0x04
IFR_FORM_SET_OP = 0x0E


def find_form_packages(data):
    """Return [(offset, bytes)] for every IFR form package (header included)."""
    res = []
    pos = 0
    while True:
        i = data.find(bytes([IFR_FORM_SET_OP]), pos)
        if i < 0:
            break
        off = i - 4
        if off >= 0:
            hdr = struct.unpack_from('<I', data, off)[0]
            plen, ptype = hdr & 0xFFFFFF, hdr >> 24
            oplen = data[i + 1] & 0x7F
            if (ptype == HII_PACKAGE_FORMS and 0x20 < plen <= len(data) - off
                    and oplen >= 0x17 and data[i + 1] & 0x80):
                # The package must end with an END opcode (0x29 0x02).
                if data[off + plen - 2:off + plen] == b'\x29\x02':
                    res.append((off, data[off:off + plen]))
                    pos = off + plen
                    continue
        pos = i + 1
    return res


def find_string_packages(data):
    """Return [(offset, language, {id: str}, package_bytes)]."""
    res = []
    i = 0
    while True:
        i = data.find(b'\x34\x00\x00\x00\x34\x00\x00\x00', i)
        if i < 0:
            break
        off = i - 4
        if off >= 0:
            hdr = struct.unpack_from('<I', data, off)[0]
            plen, ptype = hdr & 0xFFFFFF, hdr >> 24
            if ptype == HII_PACKAGE_STRINGS and 0x40 < plen <= len(data) - off:
                lang = data[off + 0x2E:off + plen].split(b'\0')[0]
                try:
                    strings = parse_string_blocks(data[off + 0x34:off + plen])
                    res.append((off, lang.decode('ascii'), strings, data[off:off + plen]))
                    i = off + plen
                    continue
                except (ValueError, IndexError, UnicodeDecodeError):
                    pass
        i += 4
    return res


def _ucs2z(b, p):
    e = p
    while b[e:e + 2] != b'\0\0':
        e += 2
    return b[p:e].decode('utf-16le', 'replace'), e + 2


def parse_string_blocks(b):
    s = {}
    sid = 1
    p = 0
    while p < len(b):
        t = b[p]
        if t == 0x00:                       # SIBT_END
            break
        elif t == 0x14:                     # SIBT_STRING_UCS2
            s[sid], p = _ucs2z(b, p + 1); sid += 1
        elif t == 0x15:                     # SIBT_STRING_UCS2_FONT
            s[sid], p = _ucs2z(b, p + 2); sid += 1
        elif t in (0x16, 0x17):             # SIBT_STRINGS_UCS2(_FONT)
            q = p + (1 if t == 0x16 else 2)
            cnt = struct.unpack_from('<H', b, q)[0]
            q += 2
            for _ in range(cnt):
                s[sid], q = _ucs2z(b, q); sid += 1
            p = q
        elif t in (0x10, 0x11):             # SIBT_STRING_SCSU(_FONT)
            q = p + (1 if t == 0x10 else 2)
            e = b.index(b'\0', q)
            s[sid] = b[q:e].decode('latin-1'); sid += 1; p = e + 1
        elif t in (0x12, 0x13):             # SIBT_STRINGS_SCSU(_FONT)
            q = p + (1 if t == 0x12 else 2)
            cnt = struct.unpack_from('<H', b, q)[0]
            q += 2
            for _ in range(cnt):
                e = b.index(b'\0', q)
                s[sid] = b[q:e].decode('latin-1'); sid += 1; q = e + 1
            p = q
        elif t == 0x20:                     # SIBT_DUPLICATE
            s[sid] = s.get(struct.unpack_from('<H', b, p + 1)[0], ''); sid += 1; p += 3
        elif t == 0x21:                     # SIBT_SKIP2
            sid += struct.unpack_from('<H', b, p + 1)[0]; p += 3
        elif t == 0x22:                     # SIBT_SKIP1
            sid += b[p + 1]; p += 2
        elif t == 0x30:                     # SIBT_EXT1
            p += b[p + 2]
        elif t == 0x31:                     # SIBT_EXT2
            p += struct.unpack_from('<H', b, p + 2)[0]
        elif t == 0x32:                     # SIBT_EXT4
            p += struct.unpack_from('<I', b, p + 2)[0]
        else:
            raise ValueError('unknown string block %#x at %#x' % (t, p))
    return s


# ---------------------------------------------------------------------------
# IFR opcodes
# ---------------------------------------------------------------------------

OP = dict(
    FORM=0x01, SUBTITLE=0x02, TEXT=0x03, IMAGE=0x04, ONE_OF=0x05, CHECKBOX=0x06,
    NUMERIC=0x07, PASSWORD=0x08, ONE_OF_OPTION=0x09, SUPPRESS_IF=0x0A, LOCKED=0x0B,
    ACTION=0x0C, RESET_BUTTON=0x0D, FORM_SET=0x0E, REF=0x0F, NO_SUBMIT_IF=0x10,
    INCONSISTENT_IF=0x11, EQ_ID_VAL=0x12, EQ_ID_ID=0x13, EQ_ID_VAL_LIST=0x14,
    AND=0x15, OR=0x16, NOT=0x17, RULE=0x18, GRAY_OUT_IF=0x19, DATE=0x1A, TIME=0x1B,
    STRING=0x1C, REFRESH=0x1D, DISABLE_IF=0x1E, ANIMATION=0x1F, TO_LOWER=0x20,
    TO_UPPER=0x21, MAP=0x22, ORDERED_LIST=0x23, VARSTORE=0x24,
    VARSTORE_NAME_VALUE=0x25, VARSTORE_EFI=0x26, VARSTORE_DEVICE=0x27, VERSION=0x28,
    END=0x29, MATCH=0x2A, GET=0x2B, SET=0x2C, READ=0x2D, WRITE=0x2E, EQUAL=0x2F,
    NOT_EQUAL=0x30, GREATER_THAN=0x31, GREATER_EQUAL=0x32, LESS_THAN=0x33,
    LESS_EQUAL=0x34, BITWISE_AND=0x35, BITWISE_OR=0x36, BITWISE_NOT=0x37,
    SHIFT_LEFT=0x38, SHIFT_RIGHT=0x39, ADD=0x3A, SUBTRACT=0x3B, MULTIPLY=0x3C,
    DIVIDE=0x3D, MODULO=0x3E, RULE_REF=0x3F, QUESTION_REF1=0x40, QUESTION_REF2=0x41,
    UINT8=0x42, UINT16=0x43, UINT32=0x44, UINT64=0x45, TRUE=0x46, FALSE=0x47,
    TO_UINT=0x48, TO_STRING=0x49, TO_BOOLEAN=0x4A, MID=0x4B, FIND=0x4C, TOKEN=0x4D,
    STRING_REF1=0x4E, STRING_REF2=0x4F, CONDITIONAL=0x50, QUESTION_REF3=0x51,
    ZERO=0x52, ONE=0x53, ONES=0x54, UNDEFINED=0x55, LENGTH=0x56, DUP=0x57, THIS=0x58,
    SPAN=0x59, VALUE=0x5A, DEFAULT=0x5B, DEFAULTSTORE=0x5C, FORM_MAP=0x5D,
    CATENATE=0x5E, GUID=0x5F, SECURITY=0x60, MODAL_TAG=0x61, REFRESH_ID=0x62,
    WARNING_IF=0x63, MATCH2=0x64,
)
OPNAME = {v: k for k, v in OP.items()}

QUESTION_OPS = {OP['ONE_OF'], OP['CHECKBOX'], OP['NUMERIC'], OP['PASSWORD'], OP['ACTION'],
                OP['REF'], OP['DATE'], OP['TIME'], OP['STRING'], OP['ORDERED_LIST']}


class Node:
    __slots__ = ('op', 'body', 'children', 'offset')

    def __init__(self, op, body, offset):
        self.op = op
        self.body = body          # opcode bytes without the 2-byte header
        self.children = []
        self.offset = offset

    @property
    def name(self):
        return OPNAME.get(self.op, '%#x' % self.op)

    def u8(self, o):
        return self.body[o]

    def u16(self, o):
        return struct.unpack_from('<H', self.body, o)[0]

    def u32(self, o):
        return struct.unpack_from('<I', self.body, o)[0]

    def u64(self, o):
        return struct.unpack_from('<Q', self.body, o)[0]

    def guid(self, o):
        return uuid.UUID(bytes_le=bytes(self.body[o:o + 16]))

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def __repr__(self):
        return '<%s @%#x>' % (self.name, self.offset)


def parse_ifr(pkg):
    """Parse an IFR form package (with its 4-byte package header) into a tree.

    Returns the list of top-level nodes (normally a single FORM_SET)."""
    root = Node(-1, b'', 0)
    stack = [root]
    pos = 4
    end = len(pkg)
    while pos + 2 <= end:
        op = pkg[pos]
        ln = pkg[pos + 1] & 0x7F
        scope = pkg[pos + 1] & 0x80
        if ln < 2:
            raise ValueError('bad IFR opcode length at %#x' % pos)
        node = Node(op, pkg[pos + 2:pos + ln], pos)
        if op == OP['END']:
            if len(stack) > 1:
                stack.pop()
        else:
            stack[-1].children.append(node)
            if scope:
                stack.append(node)
        pos += ln
    return root.children
