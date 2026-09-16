"""Jockey language console — interactive REPL + one-shot execution."""

import sys
import os
import argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from lexer import Lexer
from parser import Parser
from emitter import Emitter
from runtime import JockeyVM


REPL_BANNER = """\
╔══════════════════════════════════════════╗
║   jockey-lang console v0.1              ║
║   type 'exit' to quit                   ║
╚══════════════════════════════════════════╝"""


def run_source(source: str, obfuscate: bool = False, seed = None):
    """Compile and execute a Jockey source string."""
    from main import compile_source
    prog = compile_source(source, obfuscate=obfuscate, seed=seed)
    vm = JockeyVM(prog)
    vm.run()


def repl(obfuscate: bool = False, seed = None):
    print(REPL_BANNER)
    buf = []
    while True:
        try:
            line = input('j> ')
        except (EOFError, KeyboardInterrupt):
            print()
            break
        line = line.rstrip('\n')
        if line.strip().lower() == 'exit':
            break
        if line.endswith('\\'):
            buf.append(line[:-1])
            continue
        buf.append(line)
        src = '\n'.join(buf)
        buf = []
        try:
            run_source(src, obfuscate=obfuscate, seed=seed)
        except Exception as e:
            print(f'error: {e}')


def main():
    parser = argparse.ArgumentParser(description='Jockey language console')
    parser.add_argument('file', nargs='?', help='source file to execute')
    parser.add_argument('--obfuscate', action='store_true', help='enable bytecode obfuscation')
    parser.add_argument('--seed', help='obfuscation seed (bytes)')
    args = parser.parse_args()

    seed = args.seed.encode() if args.seed else None

    if args.file:
        with open(args.file, 'r') as f:
            src = f.read()
        try:
            run_source(src, obfuscate=args.obfuscate, seed=seed)
        except Exception as e:
            print(f'error: {e}', file=sys.stderr)
            sys.exit(1)
    else:
        repl(obfuscate=args.obfuscate, seed=seed)


if __name__ == '__main__':
    main()
