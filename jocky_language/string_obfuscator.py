"""
string_obfuscator.py — compile-time string encryption for Jockey bytecode.

Usage (in emitter.py, as a new pass before serialization):
    from string_obfuscator import obfuscate_strings
    obfuscate_strings(prog)

Each string literal in the bytecode gets replaced with:
  - an encrypted blob (XOR + rotate)
  - a polymorphic decryptor call inserted before its first use
  - the original plaintext never appears in the serialized output
"""
import random
import struct

# ── XOR + rotate cipher ────────────────────────────────────────────────

def _xor_encrypt(data: bytes, key: int) -> bytes:
    return bytes(((b ^ key) + 0x33) & 0xFF for b in data)

def _xor_decrypt(data: bytes, key: int) -> bytes:
    return bytes(((b - 0x33) & 0xFF ^ key) for b in data)

def _rotate_encrypt(data: bytes, n: int) -> bytes:
    return data[n:] + data[:n]

def _rotate_decrypt(data: bytes, n: int) -> bytes:
    return data[-n:] + data[:-n]

# ── polymorphic decryptor templates ────────────────────────────────────

# Each template takes (key_reg, src_reg, dst_reg, tmp_reg)
# and emits a sequence of XOR/variable ops that look different every time.
_DECRYPTOR_TEMPLATES = [
    # Template 1: loop-based XOR
    lambda k, s, d, t: [
        ("mov", d, s),
        ("push", k),
        ("label", "dec_loop"),
        ("pop", t),
        ("test", t, t),
        ("jz",   "dec_done"),
        ("xor",  d, t),
        ("sub",  t, 1),
        ("push", t),
        ("jmp",  "dec_loop"),
        ("label", "dec_done"),
    ],
    # Template 2: additive-subtractive mask
    lambda k, s, d, t: [
        ("mov", d, s),
        ("add", d, k),
        ("sub", d, 0x33),
        ("not", d),
        ("add", d, k),
    ],
    # Template 3: two-pass with temp register shuffle
    lambda k, s, d, t: [
        ("mov", t, k),
        ("mov", d, s),
        ("shl", t, 1),
        ("xor", d, t),
        ("shr", t, 1),
        ("xor", d, t),
        ("sub", d, 0x33),
    ],
]

# ── public API ─────────────────────────────────────────────────────────

def encrypt_string(s: str) -> tuple[bytes, int, int]:
    """Encrypt a string. Returns (encrypted_bytes, xor_key, rotate_n)."""
    data = s.encode("utf-8")
    key = random.randint(1, 255)
    rot = random.randint(1, min(len(data), 16) if data else 1)
    data = _rotate_encrypt(data, rot)
    data = _xor_encrypt(data, key)
    return data, key, rot

def decrypt_string(data: bytes, key: int, rot: int) -> str:
    """Decrypt a string."""
    data = _xor_decrypt(data, key)
    data = _rotate_decrypt(data, rot)
    return data.decode("utf-8", errors="replace")

def pick_decryptor() -> list:
    """Return a random decryptor instruction sequence."""
    tmpl = random.choice(_DECRYPTOR_TEMPLATES)
    return tmpl("k0", "s0", "d0", "t0")

def obfuscate_number(n: int) -> tuple[int, int]:
    """Obfuscate a number as (encoded, xor_key)."""
    key = random.randint(1, 255)
    return (n ^ key) + 0x55, key

def deobfuscate_number(enc: int, key: int) -> int:
    return ((enc - 0x55) & 0xFFFFFFFF) ^ key
