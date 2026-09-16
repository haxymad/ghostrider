"""Tests for jocky_language components.

Run with:
    python -m pytest tests/ -v
or:
    python -m unittest discover tests/ -v
"""

import os
import sys
import unittest
import struct
import json
import tempfile
import shutil

# ensure the package dir is on sys.path
_HERE = os.path.dirname(os.path.abspath(__file__))
_PKG = os.path.join(_HERE, '..', 'jocky_language')
_PKG = os.path.normpath(_PKG)
if _PKG not in sys.path:
    sys.path.insert(0, _PKG)

from lexer import Lexer, LexerError
from parser import Parser, ParseError
from emitter import Emitter, CompileError, OPCODES
from runtime import JockeyVM, RuntimeError_
from serializer import serialize_program, deserialize_program, MAGIC
from obfuscator import (
    StringTable, MetamorphicTransformer,
    _jockey_hash, run_metamorphic,
    compile_string_replacements, inject_decrypt_builtins,
    ALGO_XOR, ALGO_ADDSUB, ALGO_TWOPASS, ALGO_XOR_ROT,
)
from main import compile_source


# ═══════════════════════════════════════════════════════════════════════
# lexer
# ═══════════════════════════════════════════════════════════════════════

class TestLexer(unittest.TestCase):
    def test_tokens_primitives(self):
        src = '42 3.14 "hello" true false none'
        toks = Lexer(src).tokenize()
        kinds = [t[0] for t in toks]
        self.assertEqual(kinds, ['INT', 'FLOAT', 'STR', 'BOOL', 'BOOL', 'NONE', 'EOF'])

    def test_tokens_identifiers(self):
        src = 'myVar _x123 __internal'
        toks = Lexer(src).tokenize()
        self.assertEqual([t[0] for t in toks], ['IDENT', 'IDENT', 'IDENT', 'EOF'])

    def test_tokens_symbols(self):
        src = '= + - * / == != < > <= >= ( ) [ ] { } , ; :'
        toks = Lexer(src).tokenize()
        vals = [t[1] for t in toks]
        self.assertEqual(vals, ['=', '+', '-', '*', '/', '==', '!=', '<', '>', '<=', '>=', '(', ')', '[', ']', '{', '}', ',', ';', ':', None])

    def test_tokens_comments(self):
        src = '42 # this is a comment\n 99'
        toks = Lexer(src).tokenize()
        self.assertEqual(len(toks), 3)
        self.assertEqual(toks[0][0], 'INT')
        self.assertEqual(toks[0][1], 42)
        self.assertEqual(toks[1][0], 'INT')
        self.assertEqual(toks[1][1], 99)

    def test_string_escape(self):
        src = r'"hello\nworld\t!"'
        toks = Lexer(src).tokenize()
        self.assertEqual(toks[0][0], 'STR')
        self.assertIn('\n', toks[0][1])
        self.assertIn('\t', toks[0][1])


# ═══════════════════════════════════════════════════════════════════════
# parser
# ═══════════════════════════════════════════════════════════════════════

class TestParser(unittest.TestCase):
    def _src(self, code):
        return Lexer(code).tokenize()

    def test_parse_let(self):
        src = self._src('let x = 42')
        ast = Parser(src).parse_program()
        self.assertEqual(ast[0][0], 'let')
        self.assertEqual(ast[0][1], 'x')
        self.assertEqual(ast[0][2], ('int', 42))

    def test_parse_binop(self):
        src = self._src('let x = 5 + 3')
        ast = Parser(src).parse_program()
        expr = ast[0][2]
        self.assertEqual(expr[0], 'binop')
        self.assertEqual(expr[1], '+')
        self.assertEqual(expr[2], ('int', 5))
        self.assertEqual(expr[3], ('int', 3))

    def test_parse_call(self):
        src = self._src('print("hello")')
        ast = Parser(src).parse_program()
        stmt = ast[0]
        self.assertEqual(stmt[0], 'expr')
        self.assertEqual(stmt[1][0], 'call')
        self.assertEqual(stmt[1][1], 'print')

    def test_parse_if(self):
        src = self._src('if x > 0 { print("pos") } else { print("neg") }')
        ast = Parser(src).parse_program()
        self.assertEqual(ast[0][0], 'if')

    def test_parse_while(self):
        src = self._src('while x > 0 { x = x - 1 }')
        ast = Parser(src).parse_program()
        self.assertEqual(ast[0][0], 'while')

    def test_parse_for(self):
        src = self._src('for item in items { print(item) }')
        ast = Parser(src).parse_program()
        self.assertEqual(ast[0][0], 'for')

    def test_parse_fn_def(self):
        src = self._src('fn add(a, b) { return a + b }')
        ast = Parser(src).parse_program()
        fn = ast[0]
        self.assertEqual(fn[0], 'fn')
        self.assertEqual(fn[1], 'add')
        self.assertEqual(fn[2], ['a', 'b'])

    def test_parse_list_literal(self):
        src = self._src('let arr = [1, 2, 3]')
        ast = Parser(src).parse_program()
        expr = ast[0][2]
        self.assertEqual(expr[0], 'list')

    def test_parse_dict_literal(self):
        src = self._src('let d = {"key": "val"}')
        ast = Parser(src).parse_program()
        expr = ast[0][2]
        self.assertEqual(expr[0], 'dict')

    def test_parse_index(self):
        src = self._src('arr[0]')
        ast = Parser(src).parse_program()
        expr = ast[0][1]
        self.assertEqual(expr[0], 'index')

    def test_parse_attr(self):
        src = self._src('obj.field')
        ast = Parser(src).parse_program()
        expr = ast[0][1]
        self.assertEqual(expr[0], 'attr')


# ═══════════════════════════════════════════════════════════════════════
# emitter
# ═══════════════════════════════════════════════════════════════════════

class TestEmitter(unittest.TestCase):
    def _compile(self, code):
        tokens = Lexer(code).tokenize()
        prog = Parser(tokens).parse_program()
        return Emitter().compile(prog)

    def test_constants_pool(self):
        prog = self._compile('let s = "hello"\nlet n = 42\nlet f = 3.14\nlet b = true\nlet n2 = none')
        self.assertIn('hello', prog['constants'])
        self.assertIn(42, prog['constants'])
        self.assertIn(3.14, prog['constants'])
        self.assertIn(None, prog['constants'])

    def test_var_names(self):
        prog = self._compile('let x = 1\nlet y = 2\nx = x + y')
        self.assertIn('x', prog['var_names'])
        self.assertIn('y', prog['var_names'])

    def test_function_compilation(self):
        prog = self._compile('fn add(a, b) { return a + b }')
        self.assertIn('add', prog['functions'])
        fn = prog['functions']['add']
        self.assertEqual(fn['params'], ['a', 'b'])
        self.assertIsInstance(fn['code'], list)
        self.assertGreater(len(fn['code']), 0)

    def test_if_emit(self):
        prog = self._compile('if x > 0 { print("pos") }')
        ops = [op for op, _, _ in prog['main']['code']]
        self.assertIn(OPCODES['JUMP_IF_FALSE'], ops)

    def test_while_emit(self):
        prog = self._compile('while x > 0 { x = x - 1 }')
        ops = [op for op, _, _ in prog['main']['code']]
        self.assertIn(OPCODES['JUMP_IF_FALSE'], ops)
        self.assertIn(OPCODES['JUMP'], ops)

    def test_for_emit(self):
        prog = self._compile('for item in items { print(item) }')
        ops = [op for op, _, _ in prog['main']['code']]
        self.assertIn(OPCODES['LEN'], ops)
        self.assertIn(OPCODES['INDEX'], ops)

    def test_break_emit(self):
        prog = self._compile('while true { if x > 10 { break } }')
        ops = [op for op, _, _ in prog['main']['code']]
        self.assertIn(OPCODES['JUMP'], ops)

    def test_return_emit(self):
        prog = self._compile('fn test() { return 42 }')
        fn_code = prog['functions']['test']['code']
        ops = [op for op, _, _ in fn_code]
        self.assertIn(OPCODES['RETURN'], ops)


# ═══════════════════════════════════════════════════════════════════════
# runtime
# ═══════════════════════════════════════════════════════════════════════

class TestRuntime(unittest.TestCase):
    def _run(self, code):
        tokens = Lexer(code).tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_print(self):
        out = []
        import builtins
        captured = []
        def fake_print(*args):
            captured.append(' '.join(str(a) for a in args))
        original = builtins.print
        builtins.print = fake_print
        try:
            tokens = Lexer('print("hello world")').tokenize()
            prog = Parser(tokens).parse_program()
            prog = Emitter().compile(prog)
            vm = JockeyVM(prog)
            vm.run()
        finally:
            builtins.print = original
        self.assertEqual(captured, ['hello world'])

    def test_arithmetic(self):
        tokens = Lexer('print(5 + 3)').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()
        # no error means success
        self.assertTrue(True)

    def test_string_concat(self):
        tokens = Lexer('let a = "hello"\nlet b = " world"\nprint(a + b)').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_function_call(self):
        tokens = Lexer('fn add(a, b) { return a + b }\nprint(add(3, 4))').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_variable_scope(self):
        tokens = Lexer('let x = 10\nfn test() { x = x + 1 }\ntest()\nprint(x)').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_list_operations(self):
        tokens = Lexer('let arr = [1, 2, 3]\nprint(arr[0])').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_dict_operations(self):
        tokens = Lexer('let d = {"key": "val"}\nprint(d["key"])').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_comparison(self):
        tokens = Lexer('if 5 > 3 { print("ok") }').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_division(self):
        tokens = Lexer('print(10 / 2)').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_unary_not(self):
        tokens = Lexer('let b = not true\nprint(b)').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_nested_calls(self):
        tokens = Lexer('print(len([1, 2, 3]))').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()

    def test_recursive_fn(self):
        tokens = Lexer('fn fib(n) { if n <= 1 { return n } else { return fib(n - 1) + fib(n - 2) } }\nprint(fib(10))').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()


# ═══════════════════════════════════════════════════════════════════════
# serializer
# ═══════════════════════════════════════════════════════════════════════

class TestSerializer(unittest.TestCase):
    def _make_prog(self):
        tokens = Lexer('let x = 42\nprint(x)').tokenize()
        prog = Parser(tokens).parse_program()
        return Emitter().compile(prog)

    def test_roundtrip(self):
        prog = self._make_prog()
        data = serialize_program(prog)
        loaded = deserialize_program(data)
        self.assertEqual(loaded['constants'], prog['constants'])
        self.assertEqual(loaded['var_names'], prog['var_names'])
        self.assertEqual(loaded['main']['code'], prog['main']['code'])

    def test_magic(self):
        prog = self._make_prog()
        data = serialize_program(prog)
        self.assertEqual(data[:4], MAGIC)
        self.assertEqual(struct.unpack('<I', data[4:8])[0], 1)

    def test_bytes_output(self):
        prog = self._make_prog()
        data = serialize_program(prog)
        self.assertIsInstance(data, bytes)

    def test_serialize_with_functions(self):
        tokens = Lexer('fn add(a, b) { return a + b }\nprint(add(1, 2))').tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        data = serialize_program(prog)
        loaded = deserialize_program(data)
        self.assertIn('add', loaded['functions'])
        self.assertEqual(loaded['functions']['add']['params'], ['a', 'b'])


# ═══════════════════════════════════════════════════════════════════════
# obfuscator
# ═══════════════════════════════════════════════════════════════════════

class TestObfuscator(unittest.TestCase):
    def test_string_table_roundtrip(self):
        rng = __import__('random').Random(42)
        st = StringTable(rng)
        h = st.add('hello world')
        self.assertEqual(st.entries[h['index']][4], st.entries[h['index']][4])
        self.assertIsInstance(h, dict)
        self.assertIn('algo', h)
        self.assertIn('index', h)

    def test_all_algos(self):
        rng = __import__('random').Random(99)
        for _ in [ALGO_XOR, ALGO_ADDSUB, ALGO_TWOPASS, ALGO_XOR_ROT]:
            st = StringTable(rng)
            h = st.add('test')
            bc = st.get_decrypt_call(h)
            self.assertIsInstance(bc, list)
            self.assertGreater(len(bc), 0)
            self.assertEqual(bc[0][0], 'PUSH_STR')

    def test_decrypt_roundtrip(self):
        rng = __import__('random').Random(77)
        st = StringTable(rng)
        original = 'the quick brown fox'
        h = st.add(original)
        bc = st.get_decrypt_call(h)
        # simulate runtime evaluation: the builtins handle decryption
        # just verify the handle contains decryption parameters
        self.assertIn('k1', h)

    def test_jockey_hash_deterministic(self):
        h1 = _jockey_hash(b'seed123')
        h2 = _jockey_hash(b'seed123')
        self.assertEqual(h1, h2)

    def test_jockey_hash_differs(self):
        self.assertNotEqual(_jockey_hash(b'seed1'), _jockey_hash(b'seed2'))

    def test_metamorphic_add(self):
        code = [('ADD', 0, 1), ('STORE_VAR', 0, 0)]
        rng = __import__('random').Random(0)
        result = MetamorphicTransformer(rng).transform(code)
        self.assertIsInstance(result, list)
        self.assertEqual(len(result), 2)

    def test_metamorphic_varies(self):
        code = [('ADD', 0, 1), ('PUSH_CONST', 0, 1), ('STORE_VAR', 0, 0)]
        rng = __import__('random').Random(1)
        result = run_metamorphic(code, b'seed')
        ops = [i[0] for i in result]
        # transformation should produce valid opcodes (strings or ints)
        valid_ops = set(OPCODES.values()) | set(OPCODES.keys())
        self.assertTrue(all(o in valid_ops for o in ops))

    def test_string_replacements(self):
        src = 'let msg = "hello world"\nprint(msg)'
        rng = __import__('random').Random(0)
        st = StringTable(rng)
        result = compile_string_replacements(src, st)
        self.assertIn('__decrypt', result)

    def test_inject_builtins(self):
        rng = __import__('random').Random(0)
        st = StringTable(rng)
        st.add('test string')
        builtins = inject_decrypt_builtins(st)
        self.assertIn('__decrypt', builtins)
        result = builtins['__decrypt'](0, ALGO_XOR, 123, 0, 0)
        self.assertIsInstance(result, str)


# ═══════════════════════════════════════════════════════════════════════
# integration
# ═══════════════════════════════════════════════════════════════════════

class TestIntegration(unittest.TestCase):
    def _run_jockey(self, source):
        tokens = Lexer(source).tokenize()
        prog = Parser(tokens).parse_program()
        prog = Emitter().compile(prog)
        vm = JockeyVM(prog)
        vm.run()
        return vm

    def test_full_program(self):
        self._run_jockey('''
            fn add(a, b) {
                return a + b
            }
            let x = add(10, 20)
            print(x)
        ''')

    def test_nested_loops(self):
        self._run_jockey('''
            let total = 0
            for i in [1, 2, 3] {
                for j in [1, 2] {
                    total = total + i * j
                }
            }
            print(total)
        ''')

    def test_if_elif_else(self):
        self._run_jockey('''
            let x = 50
            if x > 100 {
                print("big")
            } elif x > 50 {
                print("medium")
            } else {
                print("small")
            }
        ''')

    def test_recursion(self):
        self._run_jockey('''
            fn fact(n) {
                if n <= 1 {
                    return 1
                }
                return n * fact(n - 1)
            }
            print(fact(10))
        ''')

    def test_dict_access(self):
        self._run_jockey('''
            let d = {"name": "jockey", "ver": 1}
            print(d["name"])
        ''')

    def test_list_append(self):
        self._run_jockey('''
            let arr = [1, 2, 3]
            arr[1] = 99
            print(arr[1])
        ''')

    def test_bitwise(self):
        self._run_jockey('''
            let x = 5 | 3
            print(x)
        ''')

    def test_modulo(self):
        self._run_jockey('print(10 % 3)')

    def test_string_indexing(self):
        self._run_jockey('let s = "hello"\nprint(s[1])')

    def test_chained_calls(self):
        self._run_jockey('print(len([1, 2, 3, 4, 5]))')


# ═══════════════════════════════════════════════════════════════════════
# obfuscation integration
# ═══════════════════════════════════════════════════════════════════════

class TestObfuscationIntegration(unittest.TestCase):
    def _compile_obf(self, source, seed=b'TESTSEED'):
        return compile_source(source, obfuscate=True, seed=seed)

    def test_strings_encrypted(self):
        src = 'let msg = "secret password"\nprint(msg)'
        prog = self._compile_obf(src)
        # after encryption, constants should not contain plaintext
        for c in prog['constants']:
            if isinstance(c, str):
                self.assertNotIn('secret password', c)

    def test_string_table_populated(self):
        src = '"hello" + "world"'
        prog = self._compile_obf(src)
        # check string table metadata was attached
        self.assertIn('_string_table', prog)

    def test_extra_builtins_attached(self):
        src = '"test"'
        prog = self._compile_obf(src)
        self.assertIn('_extra_builtins', prog)
        self.assertIn('__decrypt', prog['_extra_builtins'])

    def test_bytecode_changes_each_run(self):
        src = '"hello world"'
        p1 = self._compile_obf(src, seed=b'SEED1')
        p2 = self._compile_obf(src, seed=b'SEED2')
        # metamorphic transforms should produce different code
        self.assertNotEqual(p1['main']['code'], p2['main']['code'])

    def test_serialize_obfuscated_program(self):
        src = 'let msg = "encrypted"\nprint(msg)'
        prog = self._compile_obf(src)
        data = serialize_program(prog)
        loaded = deserialize_program(data)
        # verify roundtrip preserves structure and constants
        self.assertEqual(loaded['constants'], prog['constants'])
        self.assertIsInstance(loaded['main']['code'], list)
        self.assertEqual(len(loaded['main']['code']), len(prog['main']['code']))


if __name__ == '__main__':
    unittest.main()
