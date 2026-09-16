"""Jockey language — command-line entry point."""
import sys
import os
import json

# Allow running this file directly from inside jocky_language/.
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

from lexer import Lexer, LexerError
from parser import Parser, ParseError
from emitter import Emitter, OPCODES, CompileError
from serializer import serialize_program, deserialize_program
from runtime import JockeyVM, RuntimeError_


# ─────────────────────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────────────────────

_INV_OPCODES = {v: k for k, v in OPCODES.items()}


def read_text(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)


def compile_source(source, obfuscate=False, seed=None):
    if obfuscate:
        import obfuscator
        import random as _random
        if seed is None:
            seed = _random.randbytes(16)
        # Phase 1: encrypt string literals before parsing
        rng = _random.Random(obfuscator._jockey_hash(seed))
        string_table = obfuscator.StringTable(rng)
        source = obfuscator.compile_string_replacements(source, string_table)
        # inject __decrypt into builtins
        decrypt_builtins = obfuscator.inject_decrypt_builtins(string_table)
        # store for later injection into runtime
        extra_builtins = decrypt_builtins
    else:
        extra_builtins = {}
        string_table = None
        seed = None

    tokens = Lexer(source).tokenize()
    program = Parser(tokens).parse_program()
    program = Emitter().compile(program)

    if obfuscate and program:
        # Phase 2: metamorphic bytecode transform
        rng2 = _random.Random(obfuscator._jockey_hash(seed + b':meta'))
        import hashlib as _hl
        meta_seed = seed + _hl.sha256(str(rng2.randint(0, 999999)).encode()).digest()[:4]
        transformed_main = obfuscator.run_metamorphic(program['main']['code'], meta_seed)
        program['main']['code'] = transformed_main
        # transform function bodies too
        for fn_name, fn_info in program.get('functions', {}).items():
            fn_info['code'] = obfuscator.run_metamorphic(fn_info['code'], meta_seed + fn_name.encode())
        # attach string table metadata for runtime
        program['_string_table'] = string_table.entries if string_table else []
        program['_extra_builtins'] = extra_builtins

    return program


def json_safe(obj):
    if isinstance(obj, (bytes, bytearray)):
        return list(obj)
    if isinstance(obj, dict):
        return {k: json_safe(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [json_safe(v) for v in obj]
    return obj


def disassemble(program):
    print("=== CONSTANTS ===")
    for i, c in enumerate(program["constants"]):
        if isinstance(c, (bytes, bytearray)):
            print(f"  [{i}] bytes len={len(c)}")
        else:
            print(f"  [{i}] {c!r}")

    print("=== VARS ===")
    for i, n in enumerate(program["var_names"]):
        print(f"  [{i}] {n}")

    for name, info in program.get("functions", {}).items():
        print(f"=== FN {name}({', '.join(info['params'])}) ===")
        for pc, (op, a1, a2) in enumerate(info["code"]):
            print(f"  {pc:04d}  {_INV_OPCODES.get(op, str(op)):14s} {a1:>4} {a2:>4}")

    print("=== MAIN ===")
    for pc, (op, a1, a2) in enumerate(program["main"]["code"]):
        print(f"  {pc:04d}  {_INV_OPCODES.get(op, str(op)):14s} {a1:>4} {a2:>4}")


# ─────────────────────────────────────────────────────────────────────────────
# Commands
# ─────────────────────────────────────────────────────────────────────────────

def cmd_compile(args):
    if len(args) != 2:
        print("usage: main.py compile <input.jk> <output.jkb>")
        return 1
    src = read_text(args[0])
    prog = compile_source(src)
    data = serialize_program(prog)
    write_bytes(args[1], data)
    print(f"wrote {args[1]} ({len(data)} bytes)")
    return 0


def cmd_run_source(path):
    src = read_text(path)
    prog = compile_source(src)
    return JockeyVM(prog).run()


def cmd_run_bytecode(path):
    data = read_bytes(path)
    prog = deserialize_program(data)
    return JockeyVM(prog).run()


def cmd_run(args):
    if len(args) != 1:
        print("usage: main.py run <file.jk|file.jkb>")
        return 1
    path = args[0]
    if path.endswith(".jkb"):
        return cmd_run_bytecode(path)
    return cmd_run_source(path)


def cmd_ast(args):
    if len(args) != 1:
        print("usage: main.py ast <file.jk>")
        return 1
    src = read_text(args[0])
    tokens = Lexer(src).tokenize()
    program = Parser(tokens).parse_program()
    print(repr(program))
    return 0


def cmd_bytecode(args):
    if len(args) != 1:
        print("usage: main.py bytecode <file.jk>")
        return 1
    src = read_text(args[0])
    prog = compile_source(src)
    print(json.dumps(json_safe(prog), indent=2))
    return 0


def cmd_disasm(args):
    if len(args) != 1:
        print("usage: main.py disasm <file.jkb>")
        return 1
    data = read_bytes(args[0])
    prog = deserialize_program(data)
    disassemble(prog)
    return 0


def cmd_repl(args):
    print("Jockey REPL — type 'exit' or Ctrl-D to quit")
    while True:
        try:
            line = input("jky> ")
        except EOFError:
            print()
            return 0
        line = line.strip()
        if line in ("", "exit", "quit"):
            if line == "":
                continue
            return 0
        try:
            prog = compile_source(line)
            JockeyVM(prog).run()
        except (LexerError, ParseError, CompileError) as e:
            print(f"compile error: {e}")
        except RuntimeError_ as e:
            print(f"runtime error: {e}")
        except Exception as e:
            print(f"error: {type(e).__name__}: {e}")
    return 0


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────

USAGE = """Jockey language toolchain

Usage:
  main.py                        start the REPL
  main.py run <file.jk|.jkb>     compile-and-run, or run bytecode
  main.py compile <in> <out>     compile .jk to .jkb
  main.py ast <file.jk>          dump parsed AST
  main.py bytecode <file.jk>     dump bytecode as JSON
  main.py disasm <file.jkb>      disassemble bytecode
  main.py help                   show this message
"""


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)

    if not argv:
        return cmd_repl([])

    cmd = argv[0]
    rest = argv[1:]

    table = {
        "run": cmd_run,
        "compile": cmd_compile,
        "ast": cmd_ast,
        "bytecode": cmd_bytecode,
        "disasm": cmd_disasm,
        "repl": cmd_repl,
        "help": lambda _a: (print(USAGE), 0)[1],
        "-h": lambda _a: (print(USAGE), 0)[1],
        "--help": lambda _a: (print(USAGE), 0)[1],
    }

    if cmd in table:
        try:
            return table[cmd](rest) or 0
        except (LexerError, ParseError, CompileError) as e:
            print(f"compile error: {e}", file=sys.stderr)
            return 2
        except RuntimeError_ as e:
            print(f"runtime error: {e}", file=sys.stderr)
            return 3
        except FileNotFoundError as e:
            print(f"file not found: {e.filename}", file=sys.stderr)
            return 4

    # Shorthand: main.py <file.jk> or <file.jkb>
    if cmd.endswith(".jkb"):
        return cmd_run_bytecode(cmd)
    if cmd.endswith(".jk"):
        return cmd_run_source(cmd)

    print(f"unknown command: {cmd!r}\n", file=sys.stderr)
    print(USAGE, file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
