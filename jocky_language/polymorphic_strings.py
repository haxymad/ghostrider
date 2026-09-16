"""
polymorphic_strings.py — polymorphic string encryption for the Jockey compiler.

This module extends the Jockey compiler with polymorphic string encryption.
At compile time, every string literal in the source is:

  1. Encrypted with a unique per-string XOR key + byte rotation
  2. A polymorphic decryptor call is injected before the string's first use
  3. The original plaintext never appears in the serialized bytecode

Three decryptor templates are available, chosen randomly per string per build.
The decryptors themselves are simple Jockey bytecode sequences that the
VM interprets at runtime — no special runtime support needed.

Usage:
    from polymorphic_strings import PolymorphicStringPass
    pass_ = PolymorphicStringPass()
    obfuscated_program = pass_.transform(compiled_program)
"""
import random
import copy

# ── encryption primitives ──────────────────────────────────────────────

def _xor_encrypt(data: bytes, key: int) -> bytes:
    return bytes(((b ^ key) + 0x33) & 0xFF for b in data)


def _xor_decrypt(data: bytes, key: int) -> bytes:
    return bytes(((b - 0x33) & 0xFF ^ key) for b in data)


def _rotate_right(data: bytes, n: int) -> bytes:
    n = n % len(data) if data else 0
    return data[-n:] + data[:-1] if n else data


def _rotate_left(data: bytes, n: int) -> bytes:
    n = n % len(data) if data else 0
    return data[n:] + data[:n] if n else data


def encrypt_string(s: str, key=None, rot=None):
    """
    Encrypt a string with XOR + rotate.
    Returns (encrypted_bytes, xor_key, rotate_n).
    """
    data = s.encode("utf-8")
    if key is None:
        key = random.randint(1, 255)
    if rot is None:
        rot = random.randint(1, min(len(data), 16)) if data else 1

    data = _rotate_right(data, rot)
    data = _xor_encrypt(data, key)
    return data, key, rot


def decrypt_string(data: bytes, key: int, rot: int) -> str:
    data = _xor_decrypt(data, key)
    data = _rotate_left(data, rot)
    return data.decode("utf-8", errors="replace")


# ── number obfuscation ─────────────────────────────────────────────────

def obfuscate_number(n: int, key=None):
    if key is None:
        key = random.randint(1, 255)
    return ((n ^ key) + 0x55) & 0xFFFFFFFF, key


def deobfuscate_number(enc: int, key: int) -> int:
    return ((enc - 0x55) & 0xFFFFFFFF) ^ key


# ── polymorphic decryptor templates ────────────────────────────────────
# Each template is a list of bytecode instruction tuples.
# They produce the same result (decrypted string on stack) but with
# different instruction shapes each time.

def _decryptor_loop_xor(key_const_idx, src_const_idx, dst_var_idx):
    """Template 1: loop-based XOR decryptor."""
    return [
        ["PUSH_CONST", dst_var_idx, 0],
        ["LOAD_CONST", src_const_idx, 0],
        ["PUSH_CONST", key_const_idx, 0],
        ["CALL_BUILTIN", "crypto_xor_str", 2],
        ["STORE_VAR", dst_var_idx, 0],
    ]


def _decryptor_addsub_mask(key_const_idx, src_const_idx, dst_var_idx):
    """Template 2: additive-subtractive mask."""
    rot = random.randint(1, 8)
    return [
        ["PUSH_CONST", dst_var_idx, 0],
        ["LOAD_CONST", src_const_idx, 0],
        ["PUSH_CONST", key_const_idx, 0],
        ["PUSH_CONST", rot, 0],
        ["CALL_BUILTIN", "crypto_decrypt_addsub", 3],
        ["STORE_VAR", dst_var_idx, 0],
    ]


def _decryptor_twopass_shift(key_const_idx, src_const_idx, dst_var_idx):
    """Template 3: two-pass shift-XOR."""
    return [
        ["PUSH_CONST", dst_var_idx, 0],
        ["LOAD_CONST", src_const_idx, 0],
        ["PUSH_CONST", key_const_idx, 0],
        ["CALL_BUILTIN", "crypto_decrypt_twopass", 2],
        ["STORE_VAR", dst_var_idx, 0],
    ]


DECRYPTOR_TEMPLATES = [
    _decryptor_loop_xor,
    _decryptor_addsub_mask,
    _decryptor_twopass_shift,
]


def pick_decryptor(key_idx, src_idx, dst_idx):
    """Return a random decryptor instruction sequence."""
    tmpl = random.choice(DECRYPTOR_TEMPLATES)
    return tmpl(key_idx, src_idx, dst_idx)


# ── polymorphic string compiler pass ───────────────────────────────────

class PolymorphicStringPass:
    """
    Compiler pass that encrypts string constants and injects
    polymorphic decryptors before their first use.

    This pass runs after the emitter produces bytecode but before
    the serializer writes the .jkb file.
    """

    def __init__(self, encrypt_strings=True, obfuscate_numbers=True):
        self.encrypt_strings = encrypt_strings
        self.obfuscate_numbers = obfuscate_numbers

    def transform(self, program):
        """
        Apply polymorphic string encryption to a compiled program.
        Returns a new program dict with encrypted constants and
        injected decryptor calls.
        """
        prog = copy.deepcopy(program)
        constants = prog["constants"]
        var_names = list(prog.get("var_names", []))

        # Track which string constants have been "unlocked" (decryptor
        # already injected before their first use site)
        unlocked_strings = set()
        unlocked_ints = set()

        # Collect all use sites per constant index
        def find_use_sites(const_idx, opcode_type):
            """Find all instructions that reference a given constant."""
            sites = []
            for section in [prog["main"]] + list(prog.get("functions", {}).values()):
                for pos, instr in enumerate(section["code"]):
                    if instr[0] == opcode_type and instr[1] == const_idx:
                        sites.append((section, pos))
            return sites

        # ── encrypt strings ─────────────────────────────────────────────
        if self.encrypt_strings:
            for ci, c in enumerate(constants):
                if not isinstance(c, str):
                    continue

                enc, key, rot = encrypt_string(c)
                constants[ci] = enc  # replace plaintext with encrypted blob

                # Find first use site and inject decryptor
                use_sites = find_use_sites(ci, 1)  # opcode 1 = PUSH_STR
                if use_sites:
                    section, pos = use_sites[0]
                    if ci not in unlocked_strings:
                        # Allocate a temp var for the decrypted string
                        tmp_var = f"_dec_{ci}"
                        if tmp_var not in var_names:
                            var_names.append(tmp_var)
                        tmp_idx = var_names.index(tmp_var)

                        # Pick a random decryptor template
                        key_const_idx = self._add_temp_constant(prog, key)
                        decryptor = pick_decryptor(key_const_idx, ci, tmp_idx)

                        # Inject decryptor before first use
                        section["code"][pos:pos] = decryptor

                        # Replace all subsequent PUSH_STR uses with LOAD_VAR
                        for sec, p in use_sites[1:]:
                            sec["code"][p] = ["LOAD_VAR", tmp_idx, 0]

                        unlocked_strings.add(ci)

        # ── obfuscate numbers ───────────────────────────────────────────
        if self.obfuscate_numbers:
            for ci, c in enumerate(constants):
                if isinstance(c, bool) or not isinstance(c, int):
                    continue

                enc, key = obfuscate_number(c)
                constants[ci] = enc

                use_sites = find_use_sites(ci, 2)  # opcode 2 = PUSH_INT
                if use_sites:
                    section, pos = use_sites[0]
                    if ci not in unlocked_ints:
                        key_const_idx = self._add_temp_constant(prog, key)
                        # Set XOR key on the first PUSH_INT instruction
                        section["code"][pos] = ["PUSH_INT", ci, key]

                        # Also obfuscate subsequent uses
                        for sec, p in use_sites[1:]:
                            sec["code"][p] = ["PUSH_INT", ci, key]

                        unlocked_ints.add(ci)

        prog["var_names"] = var_names
        return prog

    def _add_temp_constant(self, program, value):
        """Add a temporary integer constant and return its index."""
        constants = program["constants"]
        constants.append(value)
        return len(constants) - 1


# ── standalone transform function ──────────────────────────────────────

def polymorphic_transform(program, encrypt_strings=True, obfuscate_numbers=True):
    """
    Convenience function: apply polymorphic string/number encryption.

    Usage:
        from polymorphic_strings import polymorphic_transform
        obfuscated = polymorphic_transform(compiled_program)
    """
    pass_ = PolymorphicStringPass(
        encrypt_strings=encrypt_strings,
        obfuscate_numbers=obfuscate_numbers,
    )
    return pass_.transform(program)
