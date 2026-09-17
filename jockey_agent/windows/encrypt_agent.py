#!/usr/bin/env python3
"""
encrypt_agent.py — encrypt jockey_agent.exe in-place (or to a new file).

Usage:
    python encrypt_agent.py out\jockey_agent.exe [out\jockey_agent.enc]

The encrypted binary is self-decrypting: it decrypts its own payload
section at runtime and executes in-process.  No runner PE on disk.
"""

import sys
import os
import struct

RC4_KEY = b"jky-agent-2026"
XOR_KEY = 0x5a


def rc4(data: bytes, key: bytes) -> bytes:
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xff
        S[i], S[j] = S[j], S[i]
    out = bytearray(len(data))
    a = b = 0
    for k in range(len(data)):
        a = (a + 1) & 0xff
        b = (b + S[a]) & 0xff
        S[a], S[b] = S[b], S[a]
        out[k] = data[k] ^ S[(S[a] + S[b]) & 0xff]
    return bytes(out)


def encrypt(data: bytes) -> bytes:
    xored = bytearray(b ^ XOR_KEY for b in data)
    return rc4(bytes(xored), RC4_KEY)


def main():
    if len(sys.argv) < 2:
        print("usage: encrypt_agent.py <agent.exe> [output.enc]")
        sys.exit(1)

    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else src

    with open(src, "rb") as f:
        plain = f.read()

    enc = encrypt(plain)
    with open(dst, "wb") as f:
        f.write(enc)

    print(f"encrypted: {len(plain)} -> {len(enc)} bytes -> {dst}")


if __name__ == "__main__":
    main()
