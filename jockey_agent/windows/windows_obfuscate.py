"""
windows_obfuscate.py — Windows agent obfuscation build step.

Chains the Jockey language metamorphic + string encryption passes
on compiled bytecode before it gets shipped to the Windows agent.

Usage:
    from windows_obfuscate import obfuscate_jkb
    with open("payload.jkb", "rb") as f:
        clean = f.read()
    dirty = obfuscate_jkb(clean, level="high")

    with open("payload_obfuscated.jkb", "wb") as f:
        f.write(dirty)
"""
import os, sys, struct, random

_LANG = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "jocky_language",
)
if _LANG not in sys.path:
    sys.path.insert(0, _LANG)

from serializer import serialize_program, deserialize_program
from metamorphic import metamorphize
from string_obfuscator import encrypt_string, obfuscate_number


def obfuscate_jkb(bytecode: bytes, level: str = "high") -> bytes:
    """
    Obfuscate a JKB1 bytecode blob for Windows deployment.

    Applies:
      1. Metamorphic instruction transformation
      2. XOR+rotate string encryption with random per-string keys
      3. Integer constant obfuscation

    Returns new bytecode blob. No plaintext strings or recognizable
    constants remain in the output.
    """
    if bytecode[:4] != b"JKB1":
        raise ValueError("not a JKB1 bytecode blob")

    prog = deserialize_program(bytecode)

    # ── pass 1: metamorphic on every function + main ───────────────────
    for section_name, section in [("main", prog["main"])] + \
            list(prog.get("functions", {}).items()):
        section["code"] = metamorphize(
            section["code"],
            prog["constants"],
            level=level,
        )

    # ── pass 2: encrypt every string constant ──────────────────────────
    for ci, c in enumerate(prog["constants"]):
        if not isinstance(c, str):
            continue
        enc, key, rot = encrypt_string(c)
        prog["constants"][ci] = enc
        # set XOR key on first PUSH_STR that references this constant
        for section in [prog["main"]] + list(prog.get("functions", {}).values()):
            for instr in section["code"]:
                if instr[0] == 1 and instr[1] == ci:
                    instr[2] = key
                    break

    # ── pass 3: obfuscate integer constants ────────────────────────────
    for ci, c in enumerate(prog["constants"]):
        if isinstance(c, bool) or not isinstance(c, int):
            continue
        enc, key = obfuscate_number(c)
        prog["constants"][ci] = enc
        for section in [prog["main"]] + list(prog.get("functions", {}).values()):
            for instr in section["code"]:
                if instr[0] == 2 and instr[1] == ci:
                    instr[2] = key
                    break

    return serialize_program(prog)


def obfuscate_file(path_in: str, path_out: str = None, level: str = "high") -> str:
    """
    Read a .jkb file, obfuscate it, write result.

    Returns the output path. If path_out is None, writes to
    <path_in>.obf.jkb
    """
    with open(path_in, "rb") as f:
        data = f.read()

    obf = obfuscate_jkb(data, level=level)

    if path_out is None:
        path_out = path_in + ".obf.jkb"

    with open(path_out, "wb") as f:
        f.write(obf)

    return path_out


def batch_obfuscate(directory: str, level: str = "high") -> list:
    """
    Obfuscate every .jkb file in a directory.

    Returns list of output paths created.
    """
    outputs = []
    for name in os.listdir(directory):
        if not name.endswith(".jkb"):
            continue
        full = os.path.join(directory, name)
        if name.endswith(".obf.jkb"):
            continue
        out = obfuscate_file(full, level=level)
        outputs.append(out)
    return outputs


def obfuscation_stats(original: bytes, obfuscated: bytes) -> dict:
    """
    Compare two bytecode blobs and return obfuscation coverage stats.
    """
    p1 = deserialize_program(original)
    p2 = deserialize_program(obfuscated)

    def count_readable_strings(constants):
        count = 0
        for c in constants:
            if isinstance(c, str) and len(c) > 2:
                count += 1
        return count

    def count_obfuscated(constants):
        count = 0
        for c in constants:
            if isinstance(c, (bytes, bytearray)):
                count += 1
        return count

    orig_strings = count_readable_strings(p1["constants"])
    obf_strings = count_readable_strings(p2["constants"])
    obf_blobs = count_obfuscated(p2["constants"])

    orig_instrs = sum(len(s["code"]) for s in [p1["main"]] + list(p1.get("functions", {}).values()))
    obf_instrs = sum(len(s["code"]) for s in [p2["main"]] + list(p2.get("functions", {}).values()))

    return {
        "original_readable_strings": orig_strings,
        "remaining_readable_strings": obf_strings,
        "encrypted_blobs": obf_blobs,
        "original_instructions": orig_instrs,
        "obfuscated_instructions": obf_instrs,
        "instruction_growth": obf_instrs - orig_instrs,
        "growth_pct": ((obf_instrs - orig_instrs) / max(orig_instrs, 1)) * 100,
    }
