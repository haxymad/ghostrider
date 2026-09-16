"""Jockey string obfuscator — polymorphic encryptor + metamorphic bytecode transforms.

Each compilation produces different:
  - encryption keys (XOR, ADD/SUB, ROT variants)
  - decryptor bytecode sequences (polymorphic stubs)
  - equivalent instruction substitutions (metamorphic)
"""

import ast_nodes as ast
import random
import struct
import hashlib
import emitter
import runtime as rt

# ── encryption algorithm pool ────────────────────────────────────────

ALGO_XOR = 0
ALGO_ADDSUB = 1
ALGO_TWOPASS = 2
ALGO_XOR_ROT = 3


def _xor_encrypt(data: bytes, key: int) -> bytes:
    return bytes(b ^ key for b in data)


def _xor_decrypt(data: bytes, key: int) -> bytes:
    return _xor_encrypt(data, key)


def _addsub_encrypt(data: bytes, key: int) -> bytes:
    return bytes(((b + key) & 0xFF) ^ 0xAA for b in data)


def _addsub_decrypt(data: bytes, key: int) -> bytes:
    return bytes(((b ^ 0xAA) - key) & 0xFF for b in data)


def _twopass_encrypt(data: bytes, k1: int, k2: int) -> bytes:
    step1 = bytes(((b + k1) & 0xFF) for b in data)
    step2 = bytes(((b ^ k2) for b in step1))
    return step2


def _twopass_decrypt(data: bytes, k1: int, k2: int) -> bytes:
    step1 = bytes(((b ^ k2) for b in data))
    step2 = bytes(((b - k1) & 0xFF) for b in step1)
    return step2


def _xor_rot_encrypt(data: bytes, key: int, rot: int) -> bytes:
    xored = bytes(b ^ key for b in data)
    r = rot % len(xored) if xored else 0
    return xored[r:] + xored[:r]


def _xor_rot_decrypt(data: bytes, key: int, rot: int) -> bytes:
    r = rot % len(data) if data else 0
    unrot = data[-r:] + data[:-r] if r else data
    return bytes(b ^ key for b in unrot)


ENCRYPT_FUNCS = {
    ALGO_XOR: _xor_encrypt,
    ALGO_ADDSUB: _addsub_encrypt,
    ALGO_TWOPASS: _twopass_encrypt,
    ALGO_XOR_ROT: _xor_rot_encrypt,
}

DECRYPT_FUNCS = {
    ALGO_XOR: 'crypto_xor',
    ALGO_ADDSUB: 'crypto_decrypt_addsub',
    ALGO_TWOPASS: 'crypto_decrypt_twopass',
    ALGO_XOR_ROT: 'crypto_xor_str',
}


# ── string table ─────────────────────────────────────────────────────

class StringTable:
    """Encrypts strings and tracks metadata for runtime decryption."""

    def __init__(self, rng: random.Random):
        self.rng = rng
        self.entries = []   # list of (algo_id, key1, key2, rot, encrypted_bytes)

    def add(self, plaintext: str) -> dict:
        """Encrypt a string and return a handle dict."""
        data = plaintext.encode('utf-8')
        algo = self.rng.choice([ALGO_XOR, ALGO_ADDSUB, ALGO_TWOPASS, ALGO_XOR_ROT])

        k1 = self.rng.randint(1, 255)
        k2 = self.rng.randint(1, 255)
        rot = self.rng.randint(1, min(len(data) - 1, 15)) if len(data) > 1 else 0

        if algo == ALGO_XOR:
            enc = _xor_encrypt(data, k1)
        elif algo == ALGO_ADDSUB:
            enc = _addsub_encrypt(data, k1)
        elif algo == ALGO_TWOPASS:
            enc = _twopass_encrypt(data, k1, k2)
        else:
            enc = _xor_rot_encrypt(data, k1, rot)

        idx = len(self.entries)
        self.entries.append((algo, k1, k2, rot, enc))
        return {
            'index': idx,
            'algo': algo,
            'k1': k1,
            'k2': k2,
            'rot': rot,
            'len': len(data),
        }

    def get_decrypt_call(self, handle: dict) -> list:
        """Emit bytecode that decrypts the string at runtime."""
        algo = handle['algo']
        idx = handle['index']
        k1 = handle['k1']
        k2 = handle['k2']
        rot = handle['rot']

        bc = []
        # load encrypted blob as bytes literal
        enc_bytes = self.entries[idx][4]
        bc.append(('PUSH_STR', enc_bytes))
        # load key material
        if algo in (ALGO_XOR, ALGO_XOR_ROT):
            bc.append(('PUSH_INT', k1))
            if algo == ALGO_XOR_ROT:
                bc.append(('PUSH_INT', rot))
            bc.append(('CALL_BUILTIN', DECRYPT_FUNCS[algo], 2 if algo == ALGO_XOR_ROT else 1))
        elif algo == ALGO_ADDSUB:
            bc.append(('PUSH_INT', k1))
            bc.append(('PUSH_INT', rot))
            bc.append(('CALL_BUILTIN', 'crypto_decrypt_addsub', 3))
        else:  # TWOPASS
            bc.append(('PUSH_INT', k1))
            bc.append(('PUSH_INT', k2))
            bc.append(('CALL_BUILTIN', 'crypto_decrypt_twopass', 2))
        return bc


OPCODES = emitter.OPCODES

# extended opcodes for metamorphic variants (must match runtime.py exactly)
_EXT_OPS = {
    'PUSH_VAR':    37, 'POP_VAR':      38,
    'ADD2':        39, 'SUB2':         40, 'MUL2':    41, 'DIV2': 42,
    'CMP2':        43,
    'ADD_CONST':   44, 'SUB_CONST':    45,
    'LOAD_CONST':  46, 'CALL_BUILTIN': 47,
}

OP_PUSH_VAR    = _EXT_OPS['PUSH_VAR']
OP_POP_VAR     = _EXT_OPS['POP_VAR']
OP_ADD2        = _EXT_OPS['ADD2']
OP_SUB2        = _EXT_OPS['SUB2']
OP_LOAD_CONST  = _EXT_OPS['LOAD_CONST']
OP_CALL_BUILTIN = _EXT_OPS['CALL_BUILTIN']


# ── metamorphic transform pass ───────────────────────────────────────

# integer opcode numbers matching the emitter
_OP_ADD    = OPCODES['ADD']
_OP_SUB    = OPCODES['SUB']
_OP_LOAD   = OPCODES['LOAD_VAR']
_OP_STORE  = OPCODES['STORE_VAR']
_OP_CALL   = OPCODES['CALL']
_OP_PUSH   = OPCODES['PUSH_CONST']

# equivalence classes: tuples of opcodes that compute the same thing
# but with different bytecode patterns
EQUIV_ADD    = (_OP_ADD, OP_ADD2)
EQUIV_SUB    = (_OP_SUB, OP_SUB2)
EQUIV_LOAD   = (_OP_LOAD, OP_PUSH_VAR)
EQUIV_STORE  = (_OP_STORE, OP_POP_VAR)
EQUIV_CALL   = (_OP_CALL, OP_CALL_BUILTIN)
EQUIV_PUSH   = (_OP_PUSH, OP_LOAD_CONST)


class MetamorphicTransformer:
    """Rewrites bytecode using equivalent instruction substitutions.

    Each compile run picks a random substitution set, so the output
    bytecode is different every time while remaining semantically identical.
    """

    def __init__(self, rng: random.Random):
        self.rng = rng
        # pick one variant from each equivalence class for this compilation
        self.subs = {
            'ADD':    self.rng.choice(EQUIV_ADD),
            'SUB':    self.rng.choice(EQUIV_SUB),
            'LOAD':   self.rng.choice(EQUIV_LOAD),
            'STORE':  self.rng.choice(EQUIV_STORE),
            'CALL':   self.rng.choice(EQUIV_CALL),
            'PUSH':   self.rng.choice(EQUIV_PUSH),
        }

    def transform(self, code: list) -> list:
        """Apply substitutions to a code list (integer opcodes)."""
        result = []
        i = 0
        while i < len(code):
            instr = code[i]
            op = instr[0]

            if op == _OP_ADD and self.subs['ADD'] == OP_ADD2:
                # convert stack-based ADD to var-based ADD2 (same semantics)
                result.append((OP_ADD2, instr[1], instr[2]))
                i += 1
                continue

            elif op == _OP_SUB and self.subs['SUB'] == OP_SUB2:
                result.append((OP_SUB2, instr[1], instr[2]))
                i += 1
                continue

            elif op == _OP_LOAD and self.subs['LOAD'] == OP_PUSH_VAR:
                result.append((OP_PUSH_VAR, instr[1], instr[2]))
                i += 1
                continue

            elif op == _OP_STORE and self.subs['STORE'] == OP_POP_VAR:
                result.append((OP_POP_VAR, instr[1], instr[2]))
                i += 1
                continue

            elif op == _OP_CALL and self.subs['CALL'] == OP_CALL_BUILTIN:
                result.append((OP_CALL_BUILTIN, instr[1], instr[2]))
                i += 1
                continue

            elif op == _OP_PUSH and self.subs['PUSH'] == OP_LOAD_CONST:
                result.append((OP_LOAD_CONST, instr[1], instr[2]))
                i += 1
                continue

            # default: keep as-is
            result.append(instr)
            i += 1

        return result


def _jockey_hash(seed: bytes) -> int:
    """Deterministic hash from seed for reproducible-but-varying transforms."""
    return int(hashlib.sha256(seed).hexdigest()[:8], 16)


# ── compile-time string replacement ──────────────────────────────────

def compile_string_replacements(source_code: str, string_table: StringTable) -> str:
    """Transform source code: replace string literals with decrypt() calls.

    This is a pre-pass that runs before parsing, so the parser never
    sees the original strings.
    """
    lines = source_code.split('\n')
    out = []
    for line in lines:
        # replace "..." string literals with decrypt(index) calls
        import re
        def replacer(m):
            s = m.group(1)
            handle = string_table.add(s)
            idx = handle['index']
            algo = handle['algo']
            k1 = handle['k1']
            rot = handle['rot']
            if algo == ALGO_XOR:
                return f'__decrypt({idx},{algo},{k1},0,0)'
            elif algo == ALGO_ADDSUB:
                return f'__decrypt({idx},{algo},{k1},0,{rot})'
            elif algo == ALGO_TWOPASS:
                k2 = handle['k2']
                return f'__decrypt({idx},{algo},{k1},{k2},0)'
            else:
                return f'__decrypt({idx},{algo},{k1},0,{rot})'

        line = re.sub(r'"([^"]*)"', replacer, line)
        out.append(line)
    return '\n'.join(out)


def inject_decrypt_builtins(string_table: StringTable) -> dict:
    """Return builtin functions that decrypt strings at runtime."""
    entries = list(string_table.entries)

    def __decrypt(idx, algo, k1, k2, rot):
        e = entries[idx]
        if algo == ALGO_XOR:
            return _xor_decrypt(e[4], k1).decode('utf-8', errors='replace')
        elif algo == ALGO_ADDSUB:
            return _addsub_decrypt(e[4], k1).decode('utf-8', errors='replace')
        elif algo == ALGO_TWOPASS:
            return _twopass_decrypt(e[4], k1, k2).decode('utf-8', errors='replace')
        else:
            return _xor_rot_decrypt(e[4], k1, rot).decode('utf-8', errors='replace')

    return {'__decrypt': __decrypt}


def run_metamorphic(code: list, seed: bytes = None) -> list:
    """Apply metamorphic transforms to compiled bytecode."""
    if seed is None:
        seed = random.randbytes(16)
    rng = random.Random(_jockey_hash(seed))
    transformer = MetamorphicTransformer(rng)
    return transformer.transform(code)
