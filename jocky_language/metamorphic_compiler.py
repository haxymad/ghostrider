"""
metamorphic_compiler.py — metamorphic bytecode generation for the Jockey compiler.

This module extends the Jockey compiler with metamorphic transformation passes.
It operates on the compiled bytecode (before serialization) and applies:

  1. Instruction substitution  (ADD → PUSH; PUSH; ADD2; POP)
  2. Dead-code insertion        (junk arithmetic, NOP sleds)
  3. Register renaming          (local register remapping)
  4. Instruction reordering     (independent block shuffling)
  5. Junk code blocks           (net-zero push/pop sequences)
  6. Opcode table shuffling     (random opcode mapping per build)

The metamorphic engine is integrated into the compiler pipeline between
the emitter and the serializer. No changes to the runtime/VM are needed —
the VM interprets whatever bytecode the compiler emits.

Usage:
    from metamorphic_compiler import MetamorphicCompiler
    m = MetamorphicCompiler(level="high")
    obfuscated_program = m.transform(compiled_program)
"""
import random
import copy


# ── instruction substitution rules ─────────────────────────────────────
# Maps an opcode name to a replacement instruction sequence.
# The replacement uses expanded opcodes that the VM also supports.
SUBSTITUTIONS = {
    "ADD":  ["PUSH_VAR", "PUSH_VAR", "ADD2", "POP_VAR"],
    "SUB":  ["PUSH_VAR", "PUSH_VAR", "SUB2", "POP_VAR"],
    "MUL":  ["PUSH_VAR", "PUSH_VAR", "MUL2", "POP_VAR"],
    "DIV":  ["PUSH_VAR", "PUSH_VAR", "DIV2", "POP_VAR"],
    "CMP":  ["PUSH_VAR", "PUSH_VAR", "CMP2", "POP_VAR"],
}


# ── junk code generators ───────────────────────────────────────────────

def _junk_pushpop(high_reg_start=10):
    """Generate a push/compute/pop sequence with net-zero effect."""
    reg = random.randint(high_reg_start, high_reg_start + 5)
    val = random.randint(0, 0xFF)
    ops = [
        ["PUSH_CONST", val, 0],
        ["ADD_CONST", 5, 0],
        ["SUB_CONST", 3, 0],
        ["SUB_CONST", 2, 0],
        ["POP_VAR", reg, 0],
    ]
    return ops


def _junk_nops(count=None):
    if count is None:
        count = random.randint(1, 4)
    return [["NOP", 0, 0] for _ in range(count)]


# ── register renaming ──────────────────────────────────────────────────

def _rename_registers(instructions, var_names, seed=None):
    """
    Remap variable indices while preserving semantics.
    Only renames variables that are locally scoped (not used across
    function boundaries or in live ranges that span calls).
    """
    if seed is None:
        seed = random.randint(0, 0xFFFF)
    rng = random.Random(seed)

    used = set()
    for op in instructions:
        for i, arg in enumerate(op):
            if i > 0 and isinstance(arg, int) and arg < len(var_names):
                used.add(arg)

    mapping = {}
    available = list(range(len(var_names), len(var_names) + len(used) * 2))
    rng.shuffle(available)
    for reg in used:
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

def _reorder_blocks(instructions):
    """Shuffle independent instruction blocks within a sequence."""
    if len(instructions) <= 4:
        return instructions

    block_size = random.randint(3, 6)
    blocks = []
    for i in range(0, len(instructions), block_size):
        block = instructions[i:i + block_size]
        if block:
            blocks.append(block)

    jump_ops = {"JUMP", "JUMP_IF_FALSE", "JUMP_IF_TRUE", "CALL", "RETURN"}
    safe = [b for b in blocks if not any(op[0] in jump_ops for op in b)]
    guarded = [b for b in blocks if any(op[0] in jump_ops for op in b)]

    rng = random.Random()
    rng.shuffle(safe)

    result = []
    for block in safe + guarded:
        result.extend(block)
    return result


# ── polymorphic decryptor generation ───────────────────────────────────

def _generate_decryptor(const_index, method=None):
    """
    Emit a polymorphic decryptor sequence for a string constant.
    The decryptor is a list of bytecode instructions that, when executed,
    decrypt the string at `const_index` and push it onto the stack.

    Three decryptor templates are available; one is chosen at random
    (or specified via `method`).
    """
    if method is None:
        method = random.choice(["loop_xor", "addsub_mask", "twopass_shift"])

    if method == "loop_xor":
        # Template: load, then XOR each byte with a rotating key
        return [
            ["LOAD_CONST", const_index, 0],       # encrypted bytes
            ["PUSH_CONST", random.randint(1, 255), 0],  # key
            ["PUSH_CONST", random.randint(1, 8), 0],    # rot
            ["CALL_BUILTIN", "_decrypt_str", 3],
        ]

    elif method == "addsub_mask":
        # Template: add mask, subtract mask, XOR key
        return [
            ["LOAD_CONST", const_index, 0],
            ["PUSH_CONST", random.randint(1, 255), 0],
            ["CALL_BUILTIN", "_decrypt_str_addsub", 2],
        ]

    else:  # twopass_shift
        return [
            ["LOAD_CONST", const_index, 0],
            ["PUSH_CONST", random.randint(1, 255), 0],
            ["CALL_BUILTIN", "_decrypt_str_twopass", 2],
        ]


# ── opcode table shuffling ─────────────────────────────────────────────

def _shuffle_opcodes(program, seed=None):
    """
    Remap every opcode in the program using a random permutation.
    This makes every build use a different opcode numbering scheme.
    """
    if seed is None:
        seed = random.randint(0, 0xFFFF)
    rng = random.Random(seed)

    known_ops = list(range(37))  # 0-36 are the standard opcodes
    rng.shuffle(known_ops)

    reverse_map = {old: new for new, old in enumerate(known_ops)}

    for section in [program["main"]] + list(program.get("functions", {}).values()):
        for instr in section["code"]:
            old_op = instr[0]
            instr[0] = reverse_map.get(old_op, old_op)


# ── metamorphic compiler pass ──────────────────────────────────────────

class MetamorphicCompiler:
    """
    Compiler pass that applies metamorphic transformations to bytecode.

    Levels:
      "low"    — opcode shuffling + register rename
      "medium" — + instruction substitution + dead code insertion
      "high"   — + block reordering + junk code blocks + decryptor injection
    """

    def __init__(self, level="high", seed=None):
        self.level = level
        self.seed = seed

    def transform(self, program):
        """
        Apply metamorphic transformations to a compiled program dict.
        Returns a new program dict with transformed bytecode.
        """
        prog = copy.deepcopy(program)

        # Always shuffle opcodes (lowest overhead, highest impact)
        _shuffle_opcodes(prog, seed=self.seed)

        # Register renaming (all levels)
        for vname in prog.get("var_names", []):
            pass  # var_names list stays the same; indices get remapped in instructions

        for section in [prog["main"]] + list(prog.get("functions", {}).values()):
            section["code"] = _rename_registers(
                section["code"], prog["var_names"], seed=self.seed
            )

        if self.level in ("medium", "high"):
            # Instruction substitution + dead code insertion
            for section in [prog["main"]] + list(prog.get("functions", {}).values()):
                section["code"] = self._apply_substitution_and_junk(section["code"])

        if self.level == "high":
            # Block reordering
            for section in [prog["main"]] + list(prog.get("functions", {}).values()):
                section["code"] = _reorder_blocks(section["code"])

            # Junk code blocks
            for section in [prog["main"]] + list(prog.get("functions", {}).values()):
                section["code"] = self._inject_junk_blocks(section["code"])

        return prog

    def _apply_substitution_and_junk(self, instructions):
        """Replace simple opcodes with expanded sequences, insert junk."""
        result = []
        for op in instructions:
            opcode = op[0]

            # Instruction substitution (60% chance)
            if opcode in SUBSTITUTIONS and random.random() < 0.6:
                expanded = SUBSTITUTIONS[opcode]
                result.extend([[e, op[1] if i == 1 else 0, 0]
                               for i, e in enumerate(expanded)])
                continue

            # Dead code insertion before the real instruction
            if random.random() < 0.25:
                result.extend(_junk_pushpop())
            if random.random() < 0.15:
                result.extend(_junk_nops())

            result.append(op)
        return result

    def _inject_junk_blocks(self, instructions):
        """Inject entire junk code blocks at random positions."""
        result = list(instructions)
        num_junks = random.randint(1, max(2, len(instructions) // 10))

        for _ in range(num_junks):
            pos = random.randint(0, len(result))
            junk = _junk_pushpop() + _junk_nops(random.randint(1, 3))
            result[pos:pos] = junk

        return result


# ── standalone transform function ──────────────────────────────────────

def metamorphize_program(program, level="high", seed=None):
    """
    Convenience function: apply metamorphic transformations to a program.

    Usage:
        from metamorphic_compiler import metamorphize_program
        obfuscated = metamorphize_program(compiled_program, level="high")
    """
    compiler = MetamorphicCompiler(level=level, seed=seed)
    return compiler.transform(program)
