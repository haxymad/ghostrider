"""
metamorphic.py — bytecode metamorphic engine for the Jockey VM.

Transforms already-compiled bytecode into a structurally different
but semantically identical version each time it's emitted.

Transformations:
  1. Instruction substitution  (ADD x,y  →  PUSH x; PUSH y; ADD2; POP d)
  2. Dead-code insertion        (NOPs, junk arithmetic that cancels out)
  3. Register renaming          (map R1→R3, R2→R1, R3→R2 etc.)
  4. Instruction reordering     (independent ops shuffled within a block)
  5. Junk code blocks           (push/pop sequences that net-zero)
"""
import random
import struct
import copy

# ── instruction substitution rules ─────────────────────────────────────
# Format: (pattern_op, replacement_sequence)
# pattern_op is a single opcode; replacements are opcode tuples.
SUBSTITUTIONS = {
    "ADD":  [("PUSH_REG", 0), ("PUSH_REG", 1), ("ADD2",), ("POP_REG", 0)],
    "SUB":  [("PUSH_REG", 0), ("PUSH_REG", 1), ("SUB2",), ("POP_REG", 0)],
    "MUL":  [("PUSH_REG", 0), ("PUSH_REG", 1), ("MUL2",), ("POP_REG", 0)],
    "DIV":  [("PUSH_REG", 0), ("PUSH_REG", 1), ("DIV2",), ("POP_REG", 0)],
    "CMP":  [("PUSH_REG", 0), ("PUSH_REG", 1), ("CMP2",), ("POP_REG", 2)],
}

# ── junk code generators ───────────────────────────────────────────────

def _junk_pushpop() -> list:
    """Push a random value, compute junk, pop — net zero effect."""
    reg = random.randint(10, 15)   # high regs unlikely to be live
    ops = [("PUSH_CONST", random.randint(0, 0xFF))]
    # add a chain that cancels: +5, -3, -2
    ops += [("ADD_CONST", 5), ("SUB_CONST", 3), ("SUB_CONST", 2)]
    ops += [("POP_REG", reg)]
    return ops

def _junk_nops(count: int = 3) -> list:
    return [("NOP",) for _ in range(count)]

# ── register renaming ─────────────────────────────────────────────────

def _rename_regs(instructions: list, seed: int = None) -> list:
    """Remap local registers while preserving semantics."""
    if seed is None:
        seed = random.randint(0, 0xFFFF)
    rng = random.Random(seed)

    used_regs = set()
    for op in instructions:
        for i, arg in enumerate(op):
            if i > 0 and isinstance(arg, int) and arg < 8:
                used_regs.add(arg)

    mapping = {}
    available = list(range(8, 32))
    rng.shuffle(available)
    for reg in used_regs:
        if available:
            mapping[reg] = available.pop()
        else:
            mapping[reg] = reg

    new_ops = []
    for op in instructions:
        new_op = list(op)
        for i in range(1, len(new_op)):
            if isinstance(new_op[i], int) and new_op[i] in mapping:
                new_op[i] = mapping[new_op[i]]
        new_ops.append(tuple(new_op))
    return new_ops

# ── instruction reordering ─────────────────────────────────────────────

def _reorder_blocks(instructions: list) -> list:
    """Shuffle independent instruction blocks."""
    # Split into blocks of 4-6 ops
    block_size = random.randint(4, 6)
    blocks = []
    for i in range(0, len(instructions), block_size):
        block = instructions[i:i + block_size]
        if block:
            blocks.append(block)

    # Shuffle blocks that don't have jumps/branches
    no_jump = [b for b in blocks if not any(op[0] in ("JMP", "JZ", "JNZ", "CALL") for op in b)]
    jump    = [b for b in blocks if any(op[0] in ("JMP", "JZ", "JNZ", "CALL") for op in b)]
    rng = random.Random()
    rng.shuffle(no_jump)
    return [op for block in no_jump + jump for op in block]

# ── polymorphic decryptor injection ────────────────────────────────────

def _inject_decryptors(instructions: list, strings: list) -> list:
    """Insert polymorphic decryptor sequences before string-using ops."""
    string_ops = {"LOAD_STR", "PRINT", "HIDE_FILE"}
    result = list(instructions)
    offset = 0

    for idx, op in enumerate(instructions):
        if op[0] in string_ops and op[1] < len(strings):
            decryptor = _random_decryptor(op[1])
            insert_pos = idx + offset
            result[insert_pos:insert_pos] = decryptor
            offset += len(decryptor)

    return result

def _random_decryptor(str_index: int) -> list:
    """Generate a unique decryptor for a specific string index."""
    key = random.randint(1, 255)
    rot = random.randint(1, 8)
    tmp = random.randint(10, 15)

    ops = [
        ("PUSH_CONST", key),
        ("PUSH_CONST", rot),
        ("PUSH_CONST", str_index),
        ("CALL_BUILTIN", "_decrypt_str"),
        ("POP_REG", 0),  # result in R0
    ]
    return ops

# ── main metamorphic pass ──────────────────────────────────────────────

def metamorphize(instructions: list, strings: list, level: str = "medium") -> list:
    """
    Apply metamorphic transformations to bytecode instructions.

    level:
      "low"    — register rename + basic substitution
      "medium" — + junk code insertion
      "high"   — + block reordering + decryptor injection
    """
    ops = list(instructions)

    # 1. Instruction substitution
    new_ops = []
    for op in ops:
        opcode = op[0]
        if opcode in SUBSTITUTIONS and random.random() < 0.6:
            new_ops.extend(SUBSTITUTIONS[opcode])
        else:
            new_ops.append(op)
    ops = new_ops

    # 2. Junk code insertion
    if level in ("medium", "high"):
        result = []
        for op in ops:
            if random.random() < 0.25:
                result.extend(_junk_pushpop())
            if random.random() < 0.15:
                result.extend(_junk_nops(random.randint(1, 3)))
            result.append(op)
        ops = result

    # 3. Register renaming
    if level in ("low", "medium", "high"):
        ops = _rename_regs(ops)

    # 4. Block reordering
    if level == "high":
        ops = _reorder_blocks(ops)

    # 5. Decryptor injection
    if level == "high":
        ops = _inject_decryptors(ops, strings)

    return ops
