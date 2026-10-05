#!/usr/bin/env python3
"""Turn a GIGABYTE Z790 AORUS PRO X WIFI7 BIOS image into emulator data.

Outputs (into --out):
  setup.sdb   setup forms, strings (12 languages), defaults, GUI layout
  gui.pak     boot logo, POST banner and GUI pictures as raw BGRA pixels
  font.bin    the GIGABYTE GUI font (24x24 anti-aliased glyphs)
  SdbIds.h    ids shared with the firmware source
"""

import argparse
import json
import os
import re
import struct
import sys
import unicodedata

import assets
import hii
import sdb
import uefi_image

GUID_BOOT_LOGO = '52BF7D53-79CF-4555-A517-1E8BB3042332'
GUID_GUI_IMAGES = '4B2E0988-9E44-49AE-8B77-F544B1CAF03F'
GUID_GUI_FONT = '8A0A450A-BEE9-42C1-9549-2BB8296AB3C3'
GUID_AMI_LOGO = '63819805-67BB-46EF-AA8D-1524A19A01E4'


def first_raw(files):
    for f in files:
        for s in f.walk_sections():
            if s.type in (uefi_image.SECTION_RAW, uefi_image.SECTION_FREEFORM_SUBTYPE_GUID) and s.data:
                return s.data
    return None


def jpeg_in(files):
    for f in files:
        for s in f.walk_sections():
            if s.data[:2] == b'\xff\xd8':
                return s.data
    return None


def bios_info(raw):
    info = {'model': 'Z790 AORUS PRO X WIFI7', 'bios_version': '', 'bios_date': '',
            'bios_id': '', 'board_short': ''}
    i = raw.find(b'$BDRP')
    m = re.compile(rb'([0-9A-Z]{8})(F[0-9]+[a-z]?)\x00(..)(.)(.)', re.S).search(raw, i if i >= 0 else 0)
    if m:
        info['bios_id'] = m.group(1).decode()
        info['bios_version'] = m.group(2).decode()
        year = struct.unpack('<H', m.group(3))[0]
        info['bios_date'] = '%02d/%02d/%04d' % (m.group(4)[0], m.group(5)[0], year)
        n = re.compile(rb'(Z[0-9]{3}[ A-Z0-9]+?)\x00').search(raw, m.end(), m.end() + 64)
        if n:
            short = n.group(1).decode()
            info['board_short'] = short
            info['model'] = re.sub(r'\bA\b', 'AORUS', short)
    return info


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bios', default='build/bios/bios.bin')
    ap.add_argument('--out', default='build/data')
    args = ap.parse_args()

    raw = open(args.bios, 'rb').read()
    img = uefi_image.Image(raw)
    print('BIOS region: %d bytes, %d firmware volumes, %d files'
          % (len(img.region), len(img.volumes), sum(1 for _ in img.files())))

    setup = max(img.find(name='Setup'), key=lambda f: len(f.body), default=None)
    tse = next(iter(img.find(name='AMITSE')), None)
    if setup is None or tse is None:
        sys.exit('Setup/AMITSE modules not found - is this an AMI Aptio V image?')
    pe = setup.section_data(uefi_image.SECTION_PE32)
    tse_pe = tse.section_data(uefi_image.SECTION_PE32)
    forms = hii.find_form_packages(pe)
    strings = hii.find_string_packages(pe)
    tse_strings = hii.find_string_packages(tse_pe)
    defaults = uefi_image.find_std_defaults(img)
    info = bios_info(raw)
    print('Setup: %d form package(s), %d languages, %d default variables'
          % (len(forms), len(strings), len(defaults)))
    print('Board: %(model)s, BIOS %(bios_version)s (%(bios_date)s), ID %(bios_id)s' % info)

    comp = sdb.Compiler(forms[0][1], strings, tse_strings, defaults, info)
    blob = comp.serialize()
    for w in comp.warnings:
        print('warning:', w)

    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'setup.sdb'), 'wb') as f:
        f.write(blob)
    with open(os.path.join(args.out, 'SdbIds.h'), 'w') as f:
        f.write(sdb.c_header_constants())

    # Pictures
    images = []
    logo = first_raw(img.find(guid=GUID_BOOT_LOGO))
    if logo is None:
        sys.exit('boot logo not found')
    logo_img = assets.open_image(logo)
    images.append((assets.IMG_BOOT_LOGO, logo_img))
    # OVMF's LogoDxe shows this while devices are connected, so the eagle
    # appears as early as on the real board.
    logo_img.convert('RGB').save(os.path.join(args.out, 'Logo.bmp'), format='BMP')
    for name, img_id in (('MyOemLogo1', assets.IMG_POST_BANNER), ('MyOemLogo2', assets.IMG_POST_BANNER_SMALL)):
        j = jpeg_in(img.find(name=name))
        if j:
            images.append((img_id, assets.open_image(j)))
    ami = first_raw(img.find(guid=GUID_AMI_LOGO))
    if ami is not None and ami[:2] == b'BM':
        images.append((assets.IMG_AMI_LOGO, assets.open_image(ami)))
    gui = first_raw(img.find(guid=GUID_GUI_IMAGES))
    pngs = assets.carve_pngs(gui)
    for idx, (_res_id, png) in enumerate(pngs):
        images.append((assets.IMG_GUI_BASE + idx, assets.open_image(png)))
    pak = assets.build_pack(images)
    with open(os.path.join(args.out, 'gui.pak'), 'wb') as f:
        f.write(pak)

    font = first_raw(img.find(guid=GUID_GUI_FONT))
    if font is None or font[8:12] != b'FONT':
        sys.exit('GUI font not found')
    needed = set()
    for pkgs in (strings, tse_strings):
        for _, _, table, _ in pkgs:
            for text in table.values():
                needed.update(ord(c) for c in unicodedata.normalize('NFC', text))
    for text in comp.extra_strings:
        needed.update(ord(c) for c in text)
    needed.update(range(0x21, 0x7F))
    needed.update(ord(c) for c in '°±µ×→←↑↓')
    font, added, unresolved = assets.merge_font(font, needed)
    print('Font: %d glyphs added from DejaVu Sans%s' % (
        added, (', %d characters without a glyph' % unresolved) if unresolved else ''))
    with open(os.path.join(args.out, 'font.bin'), 'wb') as f:
        f.write(font)

    manifest = dict(info, languages=[l for _, l, _, _ in strings], images=len(images),
                    statements=sum(len(f['stmts']) for f in comp.forms.values()),
                    sizes={n: os.path.getsize(os.path.join(args.out, n))
                           for n in ('setup.sdb', 'gui.pak', 'font.bin')})
    with open(os.path.join(args.out, 'manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=2)
    print('Wrote %s: %s' % (args.out, ', '.join('%s %d KiB' % (k, v // 1024) for k, v in manifest['sizes'].items())))


if __name__ == '__main__':
    main()
