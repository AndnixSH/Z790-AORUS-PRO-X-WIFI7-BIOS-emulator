"""Minimal, dependency-free parser for Intel/AMI UEFI flash images.

Understands just enough of the formats to dig the setup data and GUI
resources out of a GIGABYTE (AMI Aptio V) BIOS image:

  * Intel flash descriptor -> BIOS region
  * PI firmware volumes (FFSv2 / FFSv3), FFS files, sections
  * GUID-defined LZMA sections (AMI and EDK2 flavours)
  * AMI NVAR variable stores (used for the factory "StdDefaults")
"""

import lzma
import struct
import uuid

FFS2_GUID = uuid.UUID('8C8CE578-8A3D-4F1C-9935-896185C32DD3')
FFS3_GUID = uuid.UUID('5473C07A-3DCB-4DCA-BD6F-1E9689E7349A')
LZMA_GUID = uuid.UUID('EE4E5898-3914-4259-9D6E-DC7BD79403CF')
LZMAF86_GUID = uuid.UUID('D42AE6BD-1352-4BFB-909A-CA72A6EAE889')

SECTION_COMPRESSION = 0x01
SECTION_GUID_DEFINED = 0x02
SECTION_DISPOSABLE = 0x03
SECTION_PE32 = 0x10
SECTION_TE = 0x12
SECTION_VERSION = 0x14
SECTION_USER_INTERFACE = 0x15
SECTION_FIRMWARE_VOLUME_IMAGE = 0x17
SECTION_FREEFORM_SUBTYPE_GUID = 0x18
SECTION_RAW = 0x19

SECTION_NAMES = {
    0x01: 'COMPRESSION', 0x02: 'GUID_DEFINED', 0x03: 'DISPOSABLE', 0x10: 'PE32',
    0x11: 'PIC', 0x12: 'TE', 0x13: 'DXE_DEPEX', 0x14: 'VERSION', 0x15: 'UI',
    0x16: 'COMPAT16', 0x17: 'FV_IMAGE', 0x18: 'FREEFORM_GUID', 0x19: 'RAW',
    0x1B: 'PEI_DEPEX', 0x1C: 'MM_DEPEX',
}


def guid_at(data, off):
    return uuid.UUID(bytes_le=bytes(data[off:off + 16]))


def align(x, a):
    return (x + a - 1) & ~(a - 1)


class Section:
    def __init__(self, stype, data, children=None, guid=None):
        self.type = stype
        self.data = data          # section body (decompressed for encapsulations)
        self.children = children or []
        self.guid = guid          # for GUID-defined / freeform-subtype sections
        self.volume = None        # for firmware-volume-image sections

    def walk(self):
        yield self
        for c in self.children:
            yield from c.walk()

    def __repr__(self):
        return '<Section %s len=%d>' % (SECTION_NAMES.get(self.type, hex(self.type)), len(self.data))


class FfsFile:
    def __init__(self, guid, ftype, body, sections):
        self.guid = guid
        self.type = ftype
        self.body = body
        self.sections = sections

    def walk_sections(self):
        for s in self.sections:
            yield from s.walk()

    @property
    def name(self):
        for s in self.walk_sections():
            if s.type == SECTION_USER_INTERFACE:
                return s.data.decode('utf-16le', 'replace').rstrip('\0')
        return None

    def section_data(self, stype):
        for s in self.walk_sections():
            if s.type == stype:
                return s.data
        return None

    def __repr__(self):
        return '<FfsFile %s %s type=%#x>' % (self.guid, self.name, self.type)


class FirmwareVolume:
    def __init__(self, data, offset=0):
        self.data = data
        self.offset = offset
        self.files = []
        self._parse()

    def _parse(self):
        d = self.data
        self.fs_guid = guid_at(d, 0x10)
        self.length = struct.unpack_from('<Q', d, 0x20)[0]
        attrs, hlen, _cks, ext_off = struct.unpack_from('<IHHH', d, 0x2C)
        self.erase_polarity = 0xFF if attrs & 0x800 else 0x00
        if self.fs_guid not in (FFS2_GUID, FFS3_GUID):
            return
        pos = hlen
        if ext_off:
            ext_size = struct.unpack_from('<I', d, ext_off + 16)[0]
            pos = ext_off + ext_size
        pos = align(pos, 8)
        end = min(len(d), self.length)
        while pos + 24 <= end:
            hdr = d[pos:pos + 24]
            if hdr == bytes([self.erase_polarity]) * 24 or hdr == b'\xff' * 24:
                break
            name = guid_at(d, pos)
            ftype, fattr = d[pos + 18], d[pos + 19]
            size = d[pos + 20] | (d[pos + 21] << 8) | (d[pos + 22] << 16)
            hsize = 24
            if fattr & 0x01 and self.fs_guid == FFS3_GUID:   # FFS_ATTRIB_LARGE_FILE
                size = struct.unpack_from('<Q', d, pos + 24)[0]
                hsize = 32
            if size < hsize or pos + size > end:
                break
            body = d[pos + hsize:pos + size]
            sections = []
            if ftype not in (0x01, 0xF0):   # RAW and PAD files have no sections
                try:
                    sections = parse_sections(body)
                except Exception:
                    sections = []
            self.files.append(FfsFile(name, ftype, body, sections))
            pos = align(pos + size, 8)

    def walk_files(self):
        for f in self.files:
            yield f
            for s in f.walk_sections():
                if s.volume is not None:
                    yield from s.volume.walk_files()


def decompress_lzma(data):
    return lzma.decompress(data, format=lzma.FORMAT_ALONE)


def parse_sections(data):
    out = []
    pos = 0
    while pos + 4 <= len(data):
        size = data[pos] | (data[pos + 1] << 8) | (data[pos + 2] << 16)
        stype = data[pos + 3]
        hsize = 4
        if size == 0xFFFFFF:
            size = struct.unpack_from('<I', data, pos + 4)[0]
            hsize = 8
        if size < hsize or pos + size > len(data):
            break
        body = data[pos + hsize:pos + size]
        sec = Section(stype, body)
        if stype == SECTION_GUID_DEFINED:
            g = guid_at(body, 0)
            doff, _attr = struct.unpack_from('<HH', body, 16)
            sec.guid = g
            payload = data[pos + doff:pos + size]
            if g == LZMA_GUID:
                try:
                    sec.data = decompress_lzma(payload)
                    sec.children = parse_sections(sec.data)
                except lzma.LZMAError:
                    pass
            elif g == LZMAF86_GUID:
                pass    # LZMA + x86 BCJ filter: only used for PEI code, not needed here
            else:
                sec.data = payload
                try:
                    sec.children = parse_sections(payload)
                except Exception:
                    pass
        elif stype == SECTION_COMPRESSION:
            _ulen, ctype = struct.unpack_from('<IB', body, 0)
            if ctype == 0:
                sec.data = body[5:]
                sec.children = parse_sections(sec.data)
            # EFI/Tiano-compressed sections are left alone (not needed here).
        elif stype == SECTION_DISPOSABLE:
            sec.children = parse_sections(body)
        elif stype == SECTION_FIRMWARE_VOLUME_IMAGE:
            if len(body) > 0x40 and body[0x28:0x2C] == b'_FVH':
                sec.volume = FirmwareVolume(body)
        elif stype == SECTION_FREEFORM_SUBTYPE_GUID:
            sec.guid = guid_at(body, 0)
            sec.data = body[16:]
        out.append(sec)
        pos = align(pos + size, 4)
    return out


def bios_region(image):
    """Return the BIOS region of a full SPI image (or the image itself)."""
    if len(image) >= 0x1000 and struct.unpack_from('<I', image, 0x10)[0] == 0x0FF0A55A:
        flmap0 = struct.unpack_from('<I', image, 0x14)[0]
        frba = ((flmap0 >> 16) & 0xFF) << 4
        reg = struct.unpack_from('<I', image, frba + 4)[0]
        base = (reg & 0x7FFF) << 12
        limit = (((reg >> 16) & 0x7FFF) << 12) | 0xFFF
        if limit > base:
            return image[base:limit + 1]
    return image


def find_volumes(region):
    """Scan a BIOS region for top-level firmware volumes."""
    vols = []
    pos = 0
    while True:
        i = region.find(b'_FVH', pos)
        if i < 0:
            break
        start = i - 0x28
        if start >= 0 and start % 16 == 0:
            length = struct.unpack_from('<Q', region, start + 0x20)[0]
            if 0x48 <= length <= len(region) - start:
                fv = FirmwareVolume(region[start:start + length], start)
                vols.append(fv)
                pos = start + length
                continue
        pos = i + 4
    return vols


class Image:
    def __init__(self, data):
        self.raw = data
        self.region = bios_region(data)
        self.volumes = find_volumes(self.region)

    def files(self):
        for fv in self.volumes:
            yield from fv.walk_files()

    def find(self, guid=None, name=None):
        res = []
        g = uuid.UUID(guid) if isinstance(guid, str) else guid
        for f in self.files():
            if (g is None or f.guid == g) and (name is None or f.name == name):
                res.append(f)
        return res


# ---------------------------------------------------------------------------
# AMI NVAR variable stores
# ---------------------------------------------------------------------------

NVAR_RUNTIME, NVAR_ASCII_NAME, NVAR_GUID, NVAR_DATA_ONLY = 0x01, 0x02, 0x04, 0x08
NVAR_EXT_HEADER, NVAR_VALID = 0x10, 0x80


def parse_nvar_store(store):
    """Parse an AMI NVAR store. Returns a list of (name, guid, data) and
    recurses into nested stores (e.g. the "StdDefaults" variable)."""
    entries = []
    pos = 0
    # GUID store lives at the very end of the store, growing downwards.
    def guid_by_index(idx):
        off = len(store) - 16 * (idx + 1)
        return guid_at(store, off) if off >= 0 else None

    by_offset = {}
    while pos + 10 <= len(store) and store[pos:pos + 4] == b'NVAR':
        size = struct.unpack_from('<H', store, pos + 4)[0]
        nxt = store[pos + 6] | (store[pos + 7] << 8) | (store[pos + 8] << 16)
        attr = store[pos + 9]
        if size < 10:
            break
        end = pos + size
        p = pos + 10
        name = None
        guid = None
        if not attr & NVAR_DATA_ONLY:
            if attr & NVAR_GUID:
                guid = guid_at(store, p)
                p += 16
            else:
                guid = guid_by_index(store[p])
                p += 1
            if attr & NVAR_ASCII_NAME:
                e = store.index(b'\0', p)
                name = store[p:e].decode('ascii', 'replace')
                p = e + 1
            else:
                e = p
                while store[e:e + 2] != b'\0\0':
                    e += 2
                name = store[p:e].decode('utf-16le', 'replace')
                p = e + 2
        data_end = end
        if attr & NVAR_EXT_HEADER:
            ext_size = struct.unpack_from('<H', store, end - 2)[0]
            data_end = end - ext_size
        rec = {'name': name, 'guid': guid, 'data': store[p:data_end], 'attr': attr,
               'next': nxt, 'offset': pos}
        by_offset[pos] = rec
        entries.append(rec)
        pos = end
    # Follow "next" chains: the last link in the chain holds the current data.
    result = []
    for rec in entries:
        if rec['name'] is None or not rec['attr'] & NVAR_VALID:
            continue
        cur = rec
        seen = set()
        while cur['next'] != 0xFFFFFF and cur['offset'] not in seen:
            seen.add(cur['offset'])
            nxt = by_offset.get(cur['offset'] + cur['next'])
            if nxt is None:
                break
            cur = nxt
        result.append((rec['name'], rec['guid'], cur['data']))
    return result


def find_std_defaults(image):
    """Return {(name, guid): data} with the factory defaults (StdDefaults)."""
    out = {}
    for f in image.files():
        for s in f.walk_sections():
            if s.type != SECTION_RAW or not s.data.startswith(b'NVAR'):
                continue
            try:
                top = parse_nvar_store(s.data)
            except Exception:
                continue
            for name, guid, data in top:
                if name == 'StdDefaults' and data.startswith(b'NVAR'):
                    for n2, g2, d2 in parse_nvar_store(data):
                        out[(n2, str(g2).upper())] = d2
            if out:
                return out
    return out
