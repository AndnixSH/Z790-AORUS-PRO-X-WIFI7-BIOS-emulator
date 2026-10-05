#!/usr/bin/env python3
"""Drive a headless QEMU through QMP: send keys and take screenshots.

Used for automated testing of the emulator, e.g.

  scripts/qmp_drive.py --qmp build/qmp.sock \\
      wait:6 shot:post.png key:delete wait:4 shot:setup.png key:right shot:tab2.png

Actions:
  wait:SECONDS        sleep
  key:NAME[+NAME...]  press keys (QEMU key names: ret, esc, up, down, left,
                      right, delete, end, f1..f12, insert, pgup, pgdn, a, ...;
                      combos like alt+f or ctrl+s)
  type:TEXT           type characters
  shot:FILE.png       save a screenshot
  quit                stop QEMU
"""

import json
import socket
import sys
import time


class Qmp:
    def __init__(self, path, timeout=60):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(path)
                break
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.2)
        self.f = self.sock.makefile('rw')
        self._read()                       # greeting
        self.cmd('qmp_capabilities')

    def _read(self):
        while True:
            msg = json.loads(self.f.readline())
            if 'event' not in msg:
                return msg

    def cmd(self, name, **args):
        self.f.write(json.dumps({'execute': name, 'arguments': args}) + '\n')
        self.f.flush()
        reply = self._read()
        if 'error' in reply:
            raise RuntimeError('%s: %s' % (name, reply['error']))
        return reply.get('return')

    def keys(self, combo, hold_ms=80):
        names = combo.split('+')
        self.cmd('send-key', keys=[{'type': 'qcode', 'data': n} for n in names], **{'hold-time': hold_ms})


CHAR_KEYS = {' ': 'spc', '.': 'dot', '-': 'minus', '/': 'slash', ':': 'shift+semicolon',
             '+': 'shift+equal', '_': 'shift+minus'}


def main(argv):
    if len(argv) < 3 or argv[1] != '--qmp':
        sys.exit(__doc__)
    q = Qmp(argv[2])
    for action in argv[3:]:
        kind, _, arg = action.partition(':')
        if kind == 'wait':
            time.sleep(float(arg))
        elif kind == 'key':
            for k in arg.split(','):
                q.keys(k)
                time.sleep(0.25)
        elif kind == 'type':
            for ch in arg:
                if ch.isupper():
                    q.keys('shift+' + ch.lower())
                else:
                    q.keys(CHAR_KEYS.get(ch, ch))
                time.sleep(0.12)
        elif kind == 'shot':
            q.cmd('screendump', filename=arg, format='png')
            print('saved', arg)
        elif kind == 'quit':
            q.cmd('quit')
            return
        else:
            sys.exit('unknown action ' + action)


if __name__ == '__main__':
    main(sys.argv)
