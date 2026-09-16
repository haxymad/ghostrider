"""Jockey language bytecode emitter."""
import ast_nodes as ast


OPCODES = {
    'PUSH_CONST':    0, 'PUSH_STR':      1, 'PUSH_INT':      2,
    'LOAD_VAR':      3, 'STORE_VAR':     4, 'POP':           5,
    'ADD':           6, 'SUB':           7, 'MUL':           8,
    'DIV':           9, 'MOD':          10,
    'EQ':           11, 'NE':           12, 'LT':           13,
    'GT':           14, 'LE':           15, 'GE':           16,
    'AND':          17, 'OR':           18, 'NOT':          19,
    'NEG':          20, 'BIT_NOT':      21,
    'BIT_OR':       22, 'BIT_AND':      23, 'BIT_XOR':      24,
    'CALL':         25, 'LEN':          26, 'RETURN':       27,
    'GET_ATTR':     28, 'BUILD_ARRAY':  29, 'BUILD_DICT':   30,
    'JUMP':         31, 'JUMP_IF_FALSE':32, 'JUMP_IF_TRUE': 33,
    'NOP':          34, 'INDEX':        35, 'SET_INDEX':    36,
    'SHL':          48, 'SHR':          49,
}

BIN_OPS = {
    '+': 'ADD', '-': 'SUB', '*': 'MUL', '/': 'DIV', '%': 'MOD',
    '==': 'EQ', '!=': 'NE', '<': 'LT', '>': 'GT', '<=': 'LE', '>=': 'GE',
    '|': 'BIT_OR', '&': 'BIT_AND', '^': 'BIT_XOR',
    '<<': 'SHL', '>>': 'SHR',
    'and': 'AND', 'or': 'OR',
}

UN_OPS = {'-': 'NEG', '~': 'BIT_NOT', 'not': 'NOT'}


class CompileError(Exception):
    pass


class Emitter:
    def __init__(self):
        self.constants = []
        self.var_names = []
        self.functions = {}
        self.code = []
        self._temp = 0
        self._loops = []

    # ---- constant / variable pools ----

    def add_int(self, i):
        self.constants.append(int(i))
        return len(self.constants) - 1

    def add_float(self, f):
        self.constants.append(float(f))
        return len(self.constants) - 1

    def add_string(self, s):
        self.constants.append(str(s))
        return len(self.constants) - 1

    def add_none(self):
        self.constants.append(None)
        return len(self.constants) - 1

    def var(self, name):
        try:
            return self.var_names.index(name)
        except ValueError:
            self.var_names.append(name)
            return len(self.var_names) - 1

    def temp(self, tag):
        self._temp += 1
        return f"__{tag}{self._temp}"

    # ---- instructions ----

    def emit(self, op, a1=0, a2=0):
        self.code.append([OPCODES[op], a1, a2])

    def here(self):
        return len(self.code)

    def patch(self, idx, op, a1, a2=0):
        self.code[idx] = [OPCODES[op], a1, a2]

    # ---- top level ----

    def compile(self, program):
        self.constants = []
        self.var_names = []
        self.functions = {}
        self.code = []
        self._temp = 0
        self._loops = []

        body = program.body if hasattr(program, 'body') else program
        for stmt in body:
            self.compile_stmt(stmt)

        self.emit('NOP')
        return {
            'constants': list(self.constants),
            'var_names': list(self.var_names),
            'functions': {
                name: {'params': list(info['params']),
                       'code':   [list(i) for i in info['code']]}
                for name, info in self.functions.items()
            },
            'main': {'code': [list(i) for i in self.code]},
        }

    # ---- statements ----

    def _tag(self, stmt):
        if isinstance(stmt, (list, tuple)):
            return stmt[0] if stmt else None
        return type(stmt).__name__

    def compile_stmt(self, stmt):
        tag = self._tag(stmt)

        if tag == 'let':
            _, name, value = stmt
            if value is None:
                self.emit('PUSH_CONST', self.add_none())
            else:
                self.compile_expr(value)
            self.emit('STORE_VAR', self.var(name))

        elif tag == 'fn':
            _, name, params, body = stmt
            saved = self.code
            self.code = []
            for s in body:
                self.compile_stmt(s)
            self.emit('PUSH_CONST', self.add_none())
            self.emit('RETURN')
            fn_code = self.code
            self.code = saved
            self.functions[name] = {
                'params': list(params),
                'code': fn_code,
            }

        elif tag == 'if':
            _, cond, then_body, elif_parts, else_body = stmt
            self.compile_expr(cond)
            idx = self.here(); self.emit('JUMP_IF_FALSE', 0)
            for s in then_body:
                self.compile_stmt(s)
            jend = self.here(); self.emit('JUMP', 0)
            self.patch(idx, 'JUMP_IF_FALSE', self.here())
            for econd, ebody in elif_parts:
                self.compile_expr(econd)
                idx = self.here(); self.emit('JUMP_IF_FALSE', 0)
                for s in ebody:
                    self.compile_stmt(s)
                jend2 = self.here(); self.emit('JUMP', 0)
                self.patch(idx, 'JUMP_IF_FALSE', self.here())
                jf_ends = getattr(self, '_jf_ends', [])
                jf_ends.append(jend2)
                self._jf_ends = jf_ends
            for s in else_body:
                self.compile_stmt(s)
            end = self.here()
            self.patch(jend, 'JUMP', end)
            for j in getattr(self, '_jf_ends', []):
                self.patch(j, 'JUMP', end)
            self._jf_ends = []

        elif tag == 'while':
            _, cond, body = stmt
            loop_start = self.here()
            self.compile_expr(cond)
            jf = self.here(); self.emit('JUMP_IF_FALSE', 0)
            ctx = {'breaks': [], 'continues': []}
            self._loops.append(ctx)
            for s in body:
                self.compile_stmt(s)
            self._loops.pop()
            self.emit('JUMP', loop_start)
            end = self.here()
            self.patch(jf, 'JUMP_IF_FALSE', end)
            for i in ctx['breaks']:    self.patch(i, 'JUMP', end)
            for i in ctx['continues']: self.patch(i, 'JUMP', loop_start)

        elif tag == 'for':
            _, var, iterable, body = stmt
            t_iter = self.temp('iter')
            t_idx  = self.temp('idx')
            self.compile_expr(iterable)
            self.emit('STORE_VAR', self.var(t_iter))
            self.emit('PUSH_CONST', self.add_int(0))
            self.emit('STORE_VAR', self.var(t_idx))
            loop_start = self.here()
            self.emit('LOAD_VAR', self.var(t_idx))
            self.emit('LOAD_VAR', self.var(t_iter))
            self.emit('LEN')
            self.emit('LT')
            jf = self.here(); self.emit('JUMP_IF_FALSE', 0)
            self.emit('LOAD_VAR', self.var(t_iter))
            self.emit('LOAD_VAR', self.var(t_idx))
            self.emit('INDEX')
            self.emit('STORE_VAR', self.var(var))
            ctx = {'breaks': [], 'continues': []}
            self._loops.append(ctx)
            for s in body:
                self.compile_stmt(s)
            self._loops.pop()
            cont_target = self.here()
            self.emit('LOAD_VAR', self.var(t_idx))
            self.emit('PUSH_CONST', self.add_int(1))
            self.emit('ADD')
            self.emit('STORE_VAR', self.var(t_idx))
            self.emit('JUMP', loop_start)
            end = self.here()
            self.patch(jf, 'JUMP_IF_FALSE', end)
            for i in ctx['breaks']:    self.patch(i, 'JUMP', end)
            for i in ctx['continues']: self.patch(i, 'JUMP', cont_target)

        elif tag == 'return':
            _, value = stmt
            if value is None:
                self.emit('PUSH_CONST', self.add_none())
            else:
                self.compile_expr(value)
            self.emit('RETURN')

        elif tag == 'break':
            if not self._loops:
                raise CompileError("break outside loop")
            idx = self.here(); self.emit('JUMP', 0)
            self._loops[-1]['breaks'].append(idx)

        elif tag == 'continue':
            if not self._loops:
                raise CompileError("continue outside loop")
            idx = self.here(); self.emit('JUMP', 0)
            self._loops[-1]['continues'].append(idx)

        elif tag == 'block':
            _, body = stmt
            for s in body:
                self.compile_stmt(s)

        elif tag == 'expr':
            _, expr = stmt
            self.compile_expr(expr)
            self.emit('POP')

        elif tag == 'assign':
            _, op, target, value = stmt
            if op == '=':
                if isinstance(target, tuple) and target[0] == 'ident':
                    self.compile_expr(value)
                    self.emit('STORE_VAR', self.var(target[1]))
                elif isinstance(target, tuple) and target[0] == 'index':
                    self.compile_expr(target[1])
                    self.compile_expr(target[2])
                    self.compile_expr(value)
                    self.emit('SET_INDEX')
                elif isinstance(target, tuple) and target[0] == 'attr':
                    self.compile_expr(target[1])
                    self.emit('PUSH_STR', self.add_string(target[2]))
                    self.compile_expr(value)
                    self.emit('SET_INDEX')
                else:
                    raise CompileError("invalid assignment target")
                return
            # compound assignment
            if not (isinstance(target, tuple) and target[0] == 'ident'):
                raise CompileError("compound assignment only supports plain identifiers")
            binop = BIN_OPS[op[:-1]]
            self.emit('LOAD_VAR', self.var(target[1]))
            self.compile_expr(value)
            self.emit(binop)
            self.emit('STORE_VAR', self.var(target[1]))

        else:
            raise CompileError(f"unknown statement tag {tag!r}")

    # ---- expressions ----

    def _expr_tag(self, expr):
        if isinstance(expr, (list, tuple)):
            return expr[0] if expr else None
        return type(expr).__name__

    def compile_expr(self, expr):
        tag = self._expr_tag(expr)

        if tag == 'int':
            _, value = expr
            self.emit('PUSH_INT', self.add_int(value))

        elif tag == 'float':
            _, value = expr
            self.emit('PUSH_CONST', self.add_float(value))

        elif tag == 'str':
            _, value = expr
            self.emit('PUSH_STR', self.add_string(value))

        elif tag == 'bool':
            _, value = expr
            self.emit('PUSH_INT', self.add_int(1 if value else 0))

        elif tag == 'none':
            self.emit('PUSH_CONST', self.add_none())

        elif tag == 'ident':
            _, name = expr
            self.emit('LOAD_VAR', self.var(name))

        elif tag == 'binop':
            _, op, left, right = expr
            self.compile_expr(left)
            self.compile_expr(right)
            self.emit(BIN_OPS[op])

        elif tag == 'unary':
            _, op, operand = expr
            self.compile_expr(operand)
            self.emit(UN_OPS[op])

        elif tag == 'call':
            _, name, args = expr
            for a in args:
                self.compile_expr(a)
            self.emit('CALL', self.var(name), len(args))

        elif tag == 'call_expr':
            _, func, args = expr
            self.compile_expr(func)
            for a in args:
                self.compile_expr(a)
            self.emit('CALL', 0, len(args))

        elif tag == 'index':
            _, obj, idx = expr
            self.compile_expr(obj)
            self.compile_expr(idx)
            self.emit('INDEX')

        elif tag == 'attr':
            _, obj, prop = expr
            self.compile_expr(obj)
            self.emit('PUSH_STR', self.add_string(prop))
            self.emit('INDEX')

        elif tag == 'list':
            _, items = expr
            for item in items:
                self.compile_expr(item)
            self.emit('BUILD_ARRAY', len(items))

        elif tag == 'dict':
            _, pairs = expr
            for k, v in pairs:
                self.compile_expr(k)
                self.compile_expr(v)
            self.emit('BUILD_DICT', len(pairs))

        else:
            raise CompileError(f"unknown expression tag {tag!r}")

    def _emit_and(self, expr):
        _, _, left, right = expr
        self.compile_expr(left)
        jf1 = self.here(); self.emit('JUMP_IF_FALSE', 0)
        self.compile_expr(right)
        jf2 = self.here(); self.emit('JUMP_IF_FALSE', 0)
        self.emit('PUSH_INT', self.add_int(1))
        jend = self.here(); self.emit('JUMP', 0)
        l_false = self.here()
        self.emit('PUSH_INT', self.add_int(0))
        l_end = self.here()
        self.patch(jf1, 'JUMP_IF_FALSE', l_false)
        self.patch(jf2, 'JUMP_IF_FALSE', l_false)
        self.patch(jend, 'JUMP', l_end)

    def _emit_or(self, expr):
        _, _, left, right = expr
        self.compile_expr(left)
        jt1 = self.here(); self.emit('JUMP_IF_TRUE', 0)
        self.compile_expr(right)
        jt2 = self.here(); self.emit('JUMP_IF_TRUE', 0)
        self.emit('PUSH_INT', self.add_int(0))
        jend = self.here(); self.emit('JUMP', 0)
        l_true = self.here()
        self.emit('PUSH_INT', self.add_int(1))
        l_end = self.here()
        self.patch(jt1, 'JUMP_IF_TRUE', l_true)
        self.patch(jt2, 'JUMP_IF_TRUE', l_true)
        self.patch(jend, 'JUMP', l_end)
