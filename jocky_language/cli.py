import sys, json, os
from .lexer import Lexer
from .parser import Parser
from .emitter import Emitter
from .serializer import serialize_program, deserialize_program
from .runtime import JockeyVM

def compile_source(src):
    tokens = Lexer(src).tokenize()
    ast = Parser(tokens).parse_program()
    return Emitter().compile(ast)

def cmd_compile(path_in, path_out):
    with open(path_in, 'r') as f:
        src = f.read()
    prog = compile_source(src)
    data = serialize_program(prog)
    with open(path_out, 'wb') as f:
        f.write(data)
    print(f"wrote {path_out} ({len(data)} bytes)")

def cmd_run_source(path):
    with open(path, 'r') as f:
        src = f.read()
    prog = compile_source(src)
    JockeyVM(prog).run()

def cmd_run_bytecode(path):
    with open(path, 'rb') as f:
        data = f.read()
    prog = deserialize_program(data)
    JockeyVM(prog).run()

def cmd_ast(path):
    with open(path, 'r') as f:
        src = f.read()
    tokens = Lexer(src).tokenize()
    ast = Parser(tokens).parse_program()
    print(repr_ast(ast))

def cmd_bytecode(path):
    with open(path, 'r') as f:
        src = f.read()
    prog = compile_source(src)
    print(json.dumps(prog, indent=2, default=lambda b: list(b) if isinstance(b, (bytes, bytearray)) else str(b)))

def repr_ast(node, indent=0):
    pad = '  ' * indent
    name = type(node).__name__
    if hasattr(node, '__dict__') and node.__dict__:
        lines = [f"{pad}{name}("]
        for k, v in node.__dict__.items():
            if isinstance(v, list):
                lines.append(f"{pad}  {k}=[")
                for item in v:
                    if hasattr(item, '__dict__'):
                        lines.append(repr_ast(item, indent + 3))
                    elif isinstance(item, tuple):
                        lines.append(f"{pad}    (")
                        for t in item:
                            lines.append(repr_ast(t, indent + 3))
                        lines.append(f"{pad}    )")
                    else:
                        lines.append(f"{pad}    {item!r}")
                lines.append(f"{pad}  ]")
            elif hasattr(v, '__dict__'):
                lines.append(f"{pad}  {k}=")
                lines.append(repr_ast(v, indent + 2))
            else:
                lines.append(f"{pad}  {k}={v!r}")
        lines.append(f"{pad})")
        return '\n'.join(lines)
    return f"{pad}{name}()"

def repl():
    print("Jockey REPL — Ctrl-D to exit")
    from .lexer import LexerError
    from .parser import ParseError
    while True:
        try:
            line = input("jky> ")
        except EOFError:
            print()
            break
        if line.strip() in ('', ':q'): continue
        try:
            prog = compile_source(line)
            JockeyVM(prog).run()
        except Exception as e:
            print(f"error: {e}")

def main(argv=None):
    argv = argv or sys.argv[1:]
    if not argv:
        repl(); return
    cmd = argv[0]
    if cmd == 'compile':
        if len(argv) != 3:
            print("usage: jockey compile input.jk output.jkb"); return
        cmd_compile(argv[1], argv[2])
    elif cmd == 'run':
        if len(argv) != 2:
            print("usage: jockey run file.jk|file.jkb"); return
        p = argv[1]
        if p.endswith('.jkb'): cmd_run_bytecode(p)
        else: cmd_run_source(p)
    elif cmd == 'ast':
        cmd_ast(argv[1])
    elif cmd == 'bytecode':
        cmd_bytecode(argv[1])
    elif cmd == 'disasm':
        with open(argv[1], 'rb') as f:
            prog = deserialize_program(f.read())
        disassemble(prog)
    else:
        # shorthand: assume a file
        if cmd.endswith('.jkb'): cmd_run_bytecode(cmd)
        elif cmd.endswith('.jk'): cmd_run_source(cmd)
        else: print(f"unknown command {cmd!r}")

def disassemble(prog):
    inv = {v: k for k, v in __import__('jocky.emitter', fromlist=['OPCODES']).OPCODES.items()}
    print("CONSTANTS:")
    for i, c in enumerate(prog['constants']):
        print(f"  [{i}] {c!r}")
    print("VARS:")
    for i, n in enumerate(prog['var_names']):
        print(f"  [{i}] {n}")
    print("FUNCTIONS:")
    for name, info in prog['functions'].items():
        print(f"  fn {name}({', '.join(info['params'])}):")
        for pc, (op, a1, a2) in enumerate(info['code']):
            print(f"    {pc:04d}  {inv.get(op, op):14s} {a1} {a2}")
    print("MAIN:")
    for pc, (op, a1, a2) in enumerate(prog['main']['code']):
        print(f"    {pc:04d}  {inv.get(op, op):14s} {a1} {a2}")

if __name__ == '__main__':
    main()
