#!/usr/bin/env python3
"""Copy the first N messages of an ITCH stream, cutting on a frame boundary.

    gzip -dc 12302019.NASDAQ_ITCH50.gz | python3 tools/cut_itch.py 50000000 > prefix.itch
"""
import struct
import sys


def main():
    want = int(sys.argv[1])
    src = sys.stdin.buffer
    dst = sys.stdout.buffer
    buf = b""
    n = 0
    while n < want:
        chunk = src.read(1 << 22)
        if not chunk:
            break
        buf += chunk
        pos = 0
        while n < want and len(buf) - pos >= 2:
            (ln,) = struct.unpack_from(">H", buf, pos)
            if len(buf) - pos < 2 + ln:
                break
            pos += 2 + ln
            n += 1
        dst.write(buf[:pos])
        buf = buf[pos:]
    print(f"cut_itch: wrote {n} messages", file=sys.stderr)


if __name__ == "__main__":
    main()
