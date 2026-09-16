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
}

BIN_OPS = {
    '+': 'ADD', '-': 'SUB', '*': 'MUL', '/': 'DIV', '%': 'MOD',
    '==': 'EQ', '!=': 'NE', '<': 'LT', '>': 'GT', '<=': 'LE', '>=': 'GE',
    '|': 'BIT_OR', '&': 'BIT_AND', '^': 'BIT_XOR',
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

        for stmt in program.body:
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

    def compile_stmt(self, stmt):
        if isinstance(stmt, ast.LetStmt):
            if stmt.value is None:
                self.emit('PUSH_CONST', self.add_none())
            else:
                self.compile_expr(stmt.value)
            self.emit('STORE_VAR', self.var(stmt.name))

        elif isinstance(stmt, ast.AssignStmt):
            self.compile_assign(stmt)

        elif isinstance(stmt, ast.ExprStmt):
            self.compile_expr(stmt.expr)
            self.emit('POP')

        elif isinstance(stmt, ast.IfStmt):
            self.compile_if(stmt)

        elif isinstance(stmt, ast.WhileStmt):
            self.compile_while(stmt)

        elif isinstance(stmt, ast.ForStmt):
            self.compile_for(stmt)

        elif isinstance(stmt, ast.FnDef):
            self.compile_fn(stmt)

        elif isinstance(stmt, ast.ReturnStmt):
            if stmt.value is None:
                self.emit('PUSH_CONST', self.add_none())
            else:
                self.compile_expr(stmt.value)
            self.emit('RETURN')

        elif isinstance(stmt, ast.BreakStmt):
            if not self._loops:
                raise CompileError("break outside loop")
            idx = self.here()
            self.emit('JUMP', 0)
            self._loops[-1]['breaks'].append(idx)

        elif isinstance(stmt, ast.ContinueStmt):
            if not self._loops:
                raise CompileError("continue outside loop")
            idx = self.here()
            self.emit('JUMP', 0)
            self._loops[-1]['continues'].append(idx)

        else:
            raise CompileError(f"unknown statement {type(stmt).__name__}")

    def compile_assign(self, stmt):
        target, op, value = stmt.target, stmt.op, stmt.value

        if op == '=':
            if isinstance(target, ast.Identifier):
                self.compile_expr(value)
                self.emit('STORE_VAR', self.var(target.name))
            elif isinstance(target, ast.IndexExpr):
                self.compile_expr(target.obj)
                self.compile_expr(target.index)
                self.compile_expr(value)
                self.emit('SET_INDEX')
            elif isinstance(target, ast.AttrExpr):
                self.compile_expr(target.obj)
                self.emit('PUSH_STR', self.add_string(target.attr))
                self.compile_expr(value)
                self.emit('SET_INDEX')
            else:
                raise CompileError("invalid assignment target")
            return

        # Compound assignment. Only simple identifiers for now.
        if not isinstance(target, ast.Identifier):
            raise CompileError("compound assignment only supports plain identifiers")

        binop = BIN_OPS[op[:-1]]
        self.emit('LOAD_VAR', self.var(target.name))
        self.compile_expr(value)
        self.emit(binop)
        self.emit('STORE_VAR', self.var(target.name))

    def compile_if(self, stmt):
        jf_ends = []

        self.compile_expr(stmt.condition)
        idx = self.here(); self.emit('JUMP_IF_FALSE', 0)
        for s in stmt.then_body:
            self.compile_stmt(s)
        jend = self.here(); self.emit('JUMP', 0)
        self.patch(idx, 'JUMP_IF_FALSE', self.here())

        for cond, body in stmt.elif_parts:
            self.compile_expr(cond)
            idx = self.here(); self.emit('JUMP_IF_FALSE', 0)
            for s in body:
                self.compile_stmt(s)
            jend2 = self.here(); self.emit('JUMP', 0)
            jf_ends.append(jend2)
            self.patch(idx, 'JUMP_IF_FALSE', self.here())

        for s in stmt.else_body:
            self.compile_stmt(s)

        end = self.here()
        self.patch(jend, 'JUMP', end)
        for j in jf_ends:
            self.patch(j, 'JUMP', end)

    def compile_while(self, stmt):
        loop_start = self.here()
        self.compile_expr(stmt.condition)
        jf = self.here(); self.emit('JUMP_IF_FALSE', 0)

        ctx = {'breaks': [], 'continues': []}
        self._loops.append(ctx)
        for s in stmt.body:
            self.compile_stmt(s)
        self._loops.pop()

        self.emit('JUMP', loop_start)
        end = self.here()

        self.patch(jf, 'JUMP_IF_FALSE', end)
        for i in ctx['breaks']:    self.patch(i, 'JUMP', end)
        for i in ctx['continues']: self.patch(i, 'JUMP', loop_start)

    def compile_for(self, stmt):
        t_iter = self.temp('iter')
        t_idx  = self.temp('idx')

        self.compile_expr(stmt.iterable)
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
        self.emit('STORE_VAR', self.var(stmt.var_name))

        ctx = {'breaks': [], 'continues': []}
        self._loops.append(ctx)
        for s in stmt.body:
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

    def compile_fn(self, stmt):
        saved = self.code
        self.code = []
        for s in stmt.body:
            self.compile_stmt(s)
        self.emit('PUSH_CONST', self.add_none())
        self.emit('RETURN')
        fn_code = self.code
        self.code = saved
        self.functions[stmt.name] = {
            'params': list(stmt.params),
            'code': fn_code,
        }

    # ---- expressions ----

    def compile_expr(self, expr):
        if isinstance(expr, ast.IntLit):
            self.emit('PUSH_INT', self.add_int(expr.value))

        elif isinstance(expr, ast.FloatLit):
            idx = self.add_string(repr(expr.value))
            self.emit('PUSH_STR', idx)
            self.emit('CALL', self.var('float'), 1)

        elif isinstance(expr, ast.StringLit):
            self.emit('PUSH_STR', self.add_string(expr.value))

        elif isinstance(expr, ast.BoolLit):
            self.emit('PUSH_INT', self.add_int(1 if expr.value else 0))

        elif isinstance(expr, ast.NoneLit):
            self.emit('PUSH_CONST', self.add_none())

        elif isinstance(expr, ast.Identifier):
            self.emit('LOAD_VAR', self.var(expr.name))

        elif isinstance(expr, ast.BinaryOp):
            if expr.op == 'and':
                self._emit_and(expr)
            elif expr.op == 'or':
                self._emit_or(expr)
            else:
                self.compile_expr(expr.left)
                self.compile_expr(expr.right)
                self.emit(BIN_OPS[expr.op])

        elif isinstance(expr, ast.UnaryOp):
            self.compile_expr(expr.operand)
            self.emit(UN_OPS[expr.op])

        elif isinstance(expr, ast.CallExpr):
            for a in expr.args:
                self.compile_expr(a)
            if not isinstance(expr.func, ast.Identifier):
                raise CompileError("only identifier calls are supported")
            self.emit('CALL', self.var(expr.func.name), len(expr.args))

        elif isinstance(expr, ast.IndexExpr):
            self.compile_expr(expr.obj)
            self.compile_expr(expr.index)
            self.emit('INDEX')

        elif isinstance(expr, ast.AttrExpr):
            self.compile_expr(expr.obj)
            self.emit('PUSH_STR', self.add_string(expr.attr))
            self.emit('INDEX')

        elif isinstance(expr, ast.ArrayLit):
            for item in expr.items:
                self.compile_expr(item)
            self.emit('BUILD_ARRAY', len(expr.items))

        elif isinstance(expr, ast.DictLit):
            for k, v in expr.pairs:
                self.compile_expr(k)
                self.compile_expr(v)
            self.emit('BUILD_DICT', len(expr.pairs))

        else:
            raise CompileError(f"unknown expression {type(expr).__name__}")

    def _emit_and(self, expr):
        self.compile_expr(expr.left)
        jf1 = self.here(); self.emit('JUMP_IF_FALSE', 0)
        self.compile_expr(expr.right)
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
        self.compile_expr(expr.left)
        jt1 = self.here(); self.emit('JUMP_IF_TRUE', 0)
        self.compile_expr(expr.right)
        jt2 = self.here(); self.emit('JUMP_IF_TRUE', 0)
        self.emit('PUSH_INT', self.add_int(0))
        jend = self.here(); self.emit('JUMP', 0)
        l_true = self.here()
        self.emit('PUSH_INT', self.add_int(1))
        l_end = self.here()
        self.patch(jt1, 'JUMP_IF_TRUE', l_true)
        self.patch(jt2, 'JUMP_IF_TRUE', l_true)
        self.patch(jend, 'JUMP', l_end)
