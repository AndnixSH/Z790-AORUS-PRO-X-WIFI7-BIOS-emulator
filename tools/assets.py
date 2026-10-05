"""Pack the GUI pictures of the BIOS into a raw-pixel image pack (gui.pak).

Every picture is decoded with Pillow and stored as 32-bit BGRA (straight
alpha), which the firmware can blit without a PNG/JPEG decoder.  The firmware
volume that carries the pack is LZMA-compressed by the EDK2 build anyway.
"""

import io
import re
import struct

from PIL import Image

PNG_SIG = b'\x89PNG\r\n\x1a\n'

# Fixed image ids (keep in sync with firmware/AorusPkg/Include/GuiImages.h)
IMG_BOOT_LOGO = 1
IMG_POST_BANNER = 2
IMG_POST_BANNER_SMALL = 3
IMG_AMI_LOGO = 4
IMG_GUI_BASE = 100        # + index of the PNG inside the GUI resource


def carve_pngs(blob):
    """GIGABYTE's GUI resource is a list of [cc 00 <id:u16> <len:u32> <png>]."""
    out = []
    for m in re.finditer(re.escape(PNG_SIG), blob):
        start = m.start()
        end = blob.find(b'IEND', start) + 8
        res_id = struct.unpack_from('<H', blob, start - 6)[0] if start >= 8 else 0
        out.append((res_id, blob[start:end]))
    return out


def to_bgra(img):
    img = img.convert('RGBA')
    r, g, b, a = img.split()
    return img.size, Image.merge('RGBA', (b, g, r, a)).tobytes()


def build_pack(images):
    """images: list of (id, PIL.Image).  Returns the pack bytes."""
    entries = []
    pixels = bytearray()
    for img_id, img in images:
        (w, h), data = to_bgra(img)
        entries.append((img_id, w, h, len(pixels), len(data)))
        pixels += data
    hdr_size = 16 + 16 * len(entries)
    out = bytearray(struct.pack('<8sII', b'AORUSPAK', 1, len(entries)))
    for img_id, w, h, off, size in entries:
        out += struct.pack('<IHHII', img_id, w, h, hdr_size + off, size)
    out += pixels
    return bytes(out)


def open_image(data):
    return Image.open(io.BytesIO(data))


# ---------------------------------------------------------------------------
# GUI font
# ---------------------------------------------------------------------------

FONT_HDR = 0x28
GLYPH = 24
FONT_FALLBACK_TTF = [
    '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
    '/usr/share/fonts/dejavu/DejaVuSans.ttf',
    '/usr/local/share/fonts/DejaVuSans.ttf',
    '/Library/Fonts/DejaVuSans.ttf',
]


def parse_font(blob):
    glyphs = {}
    off = FONT_HDR
    while off + 4 + GLYPH * GLYPH <= len(blob):
        ch, adv = struct.unpack_from('<HH', blob, off)
        glyphs[ch] = (adv, blob[off + 4:off + 4 + GLYPH * GLYPH])
        off += 4 + GLYPH * GLYPH
    return glyphs


def merge_font(blob, needed, ttf_paths=FONT_FALLBACK_TTF):
    """Add glyphs for characters the GIGABYTE font lacks (e.g. Vietnamese),
    rendered from DejaVu Sans with matching metrics.  Returns (font, added)."""
    import os
    from PIL import ImageDraw, ImageFont
    glyphs = parse_font(blob)
    missing = sorted(c for c in needed if c not in glyphs and 0x20 < c < 0xFFFF)
    ttf = next((p for p in ttf_paths if os.path.exists(p)), None)
    added = 0
    if missing and ttf:
        # Match cap height and baseline of the GIGABYTE glyphs using 'H'.
        h = glyphs[ord('H')][1]
        rows = [r for r in range(GLYPH) if any(h[r * GLYPH:(r + 1) * GLYPH])]
        cap_top, baseline = rows[0], rows[-1] + 1
        target = baseline - cap_top
        size = 10
        while size < 40:
            f = ImageFont.truetype(ttf, size)
            box = f.getbbox('H', anchor='ls')
            if -box[1] >= target:
                break
            size += 1
        font = ImageFont.truetype(ttf, size)
        for c in missing:
            im = Image.new('L', (GLYPH, GLYPH), 0)
            ImageDraw.Draw(im).text((1, baseline), chr(c), fill=255, font=font, anchor='ls')
            adv = max(1, min(GLYPH, int(round(font.getlength(chr(c)))) + 1))
            glyphs[c] = (adv, im.tobytes())
            added += 1
    out = bytearray(blob[:FONT_HDR])
    for c in sorted(glyphs):
        adv, bm = glyphs[c]
        out += struct.pack('<HH', c, adv) + bm
    return bytes(out), added, len(missing) - added
