#!/usr/bin/env python3
"""Download a Z790 AORUS PRO X WIFI7 BIOS from GIGABYTE's download server.

The BIOS is GIGABYTE's copyrighted firmware, so this repository never
contains it; every user downloads their own copy straight from GIGABYTE.
"""

import argparse
import hashlib
import io
import os
import sys
import urllib.request
import zipfile

BOARD_CODE = 'z790-a-pro-x-wifi7_8arpt709'
URL = 'https://download.gigabyte.com/FileList/BIOS/mb_bios_%s_%s.zip'
DEFAULT_VERSION = 'f9a'

# Known-good images (SHA-256 of the BIOS file inside the zip).
KNOWN = {
    'f9a': 'ba6eaf8e1236bae030b2933db30e9d00ad3be7ebf2248975c2e506040a9ab03c',
}


def download(version):
    url = URL % (BOARD_CODE, version.lower())
    req = urllib.request.Request(url, headers={
        # The download CDN only serves files to requests coming from the
        # support page; without a Referer it redirects to a dead mirror.
        'Referer': 'https://www.gigabyte.com/',
        'User-Agent': 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 '
                      '(KHTML, like Gecko) Chrome/128.0 Safari/537.36',
    })
    print('Downloading %s' % url)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--version', default=DEFAULT_VERSION,
                    help='BIOS version, e.g. f9a, f8a, f7 (default: %(default)s)')
    ap.add_argument('--out', default='build/bios', help='output directory')
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    data = download(args.version)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        names = [n for n in z.namelist() if n.upper().startswith('Z790') and z.getinfo(n).file_size >= 16 << 20]
        if not names:
            sys.exit('No BIOS image found in the zip (contents: %s)' % z.namelist())
        image = z.read(names[0])
    digest = hashlib.sha256(image).hexdigest()
    expected = KNOWN.get(args.version.lower())
    if expected and digest != expected:
        sys.exit('SHA-256 mismatch for %s: %s' % (names[0], digest))
    if not expected:
        print('note: version %s has not been tested with this emulator' % args.version)
    path = os.path.join(args.out, 'bios.bin')
    with open(path, 'wb') as f:
        f.write(image)
    print('Saved %s (%s, %d bytes, sha256 %s)' % (path, names[0], len(image), digest))


if __name__ == '__main__':
    main()
