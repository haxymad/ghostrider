"""
obfuscation_pipeline.py — chains metamorphic + string obfuscation
into a single build step for the Windows agent.

Usage:
    from obfuscation_pipeline import obfuscate_bytecode
    obfuscated = obfuscate_bytecode(source_code, level="high")
"""
import sys, os, random

# ensure sibling modules are importable
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from metamorphic import metamorphize
from string_obfuscator import encrypt_string, obfuscate_number, pick_decryptor
from serializer import serialize_program, deserialize_program
from lexer import Lexer
from parser import Parser
from emitter import Emitter


def obfuscate_bytecode(bytecode: bytes, level: str = "high") -> bytes:
    """
    Take raw JKB1 bytecode, apply metamorphic + string encryption,
    return new bytecode blob.

    level: "low" | "medium" | "high"
    """
    prog = deserialize_program(bytecode)

    # ── layer 1: metamorphic transformation ────────────────────────────
    prog["main"]["code"] = metamorphize(
        prog["main"]["code"],
        prog["constants"],
        level=level,
    )
    for fname, finfo in prog.get("functions", {}).items():
        finfo["code"] = metamorphize(
            finfo["code"], prog["constants"], level=level
        )

    # ── layer 2: string encryption ─────────────────────────────────────
    for ci, c in enumerate(prog["constants"]):
        if not isinstance(c, str):
            continue
        enc, key, rot = encrypt_string(c)
        prog["constants"][ci] = enc

        # patch PUSH_STR instructions that reference this constant
        for section in [prog["main"]] + list(prog.get("functions", {}).values()):
            for instr in section["code"]:
                if instr[0] == 1 and instr[1] == ci:
                    instr[2] = key

    # ── layer 3: number obfuscation ────────────────────────────────────
    for ci, c in enumerate(prog["constants"]):
        if isinstance(c, int) and not isinstance(c, bool):
            enc, key = obfuscate_number(c)
            prog["constants"][ci] = enc
            for section in [prog["main"]] + list(prog.get("functions", {}).values()):
                for instr in section["code"]:
                    if instr[0] == 2 and instr[1] == ci:
                        instr[2] = key

    return serialize_program(prog)


def obfuscate_source(source: str, level: str = "high") -> bytes:
    """
    Full pipeline: source text → parse → emit → obfuscate → bytes.
    """
    tokens = Lexer(source).tokenize()
    ast = Parser(tokens).parse_program()
    prog = Emitter().compile(ast)
    bytecode = serialize_program(prog)
    return obfuscate_bytecode(bytecode, level=level)


def obfuscation_report(bytecode: bytes) -> dict:
    """
    Return stats about what was obfuscated in a bytecode blob.
    """
    prog = deserialize_program(bytecode)
    report = {
        "total_instructions": 0,
        "string_constants": 0,
        "encrypted_strings": 0,
        "int_constants": 0,
        "obfuscated_ints": 0,
        "functions": len(prog.get("functions", {})),
    }

    for c in prog["constants"]:
        if isinstance(c, str):
            report["string_constants"] += 1
            if isinstance(c, (bytes, bytearray)):
                report["encrypted_strings"] += 1
        elif isinstance(c, int) and not isinstance(c, bool):
            report["int_constants"] += 1

    for section in [prog["main"]] + list(prog.get("functions", {}).values()):
        report["total_instructions"] += len(section["code"])
        for instr in section["code"]:
            if instr[0] == 1 and instr[2] != 0:
                report["encrypted_strings"] += 1
            if instr[0] == 2 and instr[2] != 0:
                report["obfuscated_ints"] += 1

    return report
