"""Jockey language parser — builds AST from tokens."""
import ast_nodes as ast
from lexer import Token


class ParseError(Exception):
    pass


BINARY_PREC = {
    'or':  1,
    'and': 2,
    '==':  4, '!=': 4, '<': 4, '>': 4, '<=': 4, '>=': 4,
    '|':   5,
    '^':   6,
    '&':   7,
    '+':   8, '-': 8,
    '*':   9, '/': 9, '%': 9,
}

ASSIGN_OPS = {'=', '+=', '-=', '*=', '/=', '%='}


class Parser:
    def __init__(self, tokens):
        self.toks = tokens
        self.pos = 0

    @property
    def cur(self):
        return self.toks[self.pos]

    def advance(self):
        t = self.toks[self.pos]
        if self.pos < len(self.toks) - 1:
            self.pos += 1
        return t

    def match(self, ttype, value=None):
        t = self.cur
        if t.type != ttype:
            return None
        if value is not None and t.value != value:
            return None
        return self.advance()

    def expect(self, ttype, value=None):
        t = self.match(ttype, value)
        if t is None:
            c = self.cur
            want = f"{ttype}" + (f" {value!r}" if value is not None else "")
            raise ParseError(
                f"expected {want}, got {c.type} {c.value!r} at {c.line}:{c.col}"
            )
        return t

    # ---------- helpers ----------

    def _is_sym(self, tok, value):
        """A token matches if its value is `value`, regardless of whether
        the lexer tagged it OP or PUNCT. Handles '=' vs '+=' cleanly."""
        return tok.value == value and tok.type in ('OP', 'PUNCT')

    # ---------- program ----------

    def parse_program(self):
        body = []
        while self.cur.type != 'EOF':
            body.append(self.parse_stmt())
        return ast.Program(body)

    # ---------- statements ----------

    def parse_stmt(self):
        t = self.cur

        if t.type == 'PUNCT' and t.value == '{':
            return self._parse_bare_block()

        if t.type == 'KEYWORD':
            if t.value == 'let':      return self.parse_let()
            if t.value == 'fn':       return self.parse_fn()
            if t.value == 'if':       return self.parse_if()
            if t.value == 'while':    return self.parse_while()
            if t.value == 'for':      return self.parse_for()
            if t.value == 'return':   return self.parse_return()
            if t.value == 'break':
                self.advance()
                return ast.BreakStmt()
            if t.value == 'continue':
                self.advance()
                return ast.ContinueStmt()

        expr = self.parse_expr()

        # Assignment: accept '=' and compound ops regardless of OP/PUNCT tag.
        if self.cur.type in ('OP', 'PUNCT') and self.cur.value in ASSIGN_OPS:
            op = self.advance().value
            val = self.parse_expr()
            if not isinstance(expr, (ast.Identifier, ast.IndexExpr, ast.AttrExpr)):
                raise ParseError(
                    f"invalid assignment target at {expr.line}:{expr.col}"
                )
            return ast.AssignStmt(expr, op, val, expr.line, expr.col)

        return ast.ExprStmt(expr, expr.line, expr.col)

    def _parse_braced_body(self):
        self.expect('PUNCT', '{')
        body = []
        while not (self.cur.type == 'PUNCT' and self.cur.value == '}'):
            if self.cur.type == 'EOF':
                raise ParseError("unexpected EOF inside block")
            body.append(self.parse_stmt())
        self.expect('PUNCT', '}')
        return body

    def _parse_bare_block(self):
        body = self._parse_braced_body()
        return ast.IfStmt(ast.BoolLit(True), body, [], [])

    def parse_let(self):
        t = self.expect('KEYWORD', 'let')
        name = self.expect('IDENT').value
        value = None
        # '=' may be tagged OP or PUNCT — accept either.
        if self.cur.type in ('OP', 'PUNCT') and self.cur.value == '=':
            self.advance()
            value = self.parse_expr()
        return ast.LetStmt(name, value, t.line, t.col)

    def parse_fn(self):
        t = self.expect('KEYWORD', 'fn')
        name = self.expect('IDENT').value
        self.expect('PUNCT', '(')
        params = []
        if not (self.cur.type == 'PUNCT' and self.cur.value == ')'):
            params.append(self.expect('IDENT').value)
            while self.match('PUNCT', ','):
                params.append(self.expect('IDENT').value)
        self.expect('PUNCT', ')')
        body = self._parse_braced_body()
        return ast.FnDef(name, params, body, t.line, t.col)

    def parse_if(self):
        t = self.expect('KEYWORD', 'if')
        cond = self.parse_expr()
        then_body = self._parse_braced_body()
        elif_parts = []
        while self.cur.type == 'KEYWORD' and self.cur.value == 'elif':
            self.advance()
            econd = self.parse_expr()
            ebody = self._parse_braced_body()
            elif_parts.append((econd, ebody))
        else_body = []
        if self.cur.type == 'KEYWORD' and self.cur.value == 'else':
            self.advance()
            else_body = self._parse_braced_body()
        return ast.IfStmt(cond, then_body, elif_parts, else_body, t.line, t.col)

    def parse_while(self):
        t = self.expect('KEYWORD', 'while')
        cond = self.parse_expr()
        body = self._parse_braced_body()
        return ast.WhileStmt(cond, body, t.line, t.col)

    def parse_for(self):
        t = self.expect('KEYWORD', 'for')
        var = self.expect('IDENT').value
        self.expect('KEYWORD', 'in')
        it = self.parse_expr()
        body = self._parse_braced_body()
        return ast.ForStmt(var, it, body, t.line, t.col)

    def parse_return(self):
        t = self.expect('KEYWORD', 'return')
        if self.cur.type == 'EOF':
            return ast.ReturnStmt(None, t.line, t.col)
        if self.cur.type == 'PUNCT' and self.cur.value in ('}', ';'):
            return ast.ReturnStmt(None, t.line, t.col)
        val = self.parse_expr()
        return ast.ReturnStmt(val, t.line, t.col)

    # ---------- expressions ----------

    def parse_expr(self):
        return self._parse_binary(1)

    def _parse_binary(self, min_prec):
        left = self._parse_unary()
        while True:
            t = self.cur
            op = None
            if t.type == 'OP' and t.value in BINARY_PREC:
                op = t.value
            elif t.type == 'KEYWORD' and t.value in ('and', 'or'):
                op = t.value
            if op is None:
                break
            prec = BINARY_PREC[op]
            if prec < min_prec:
                break
            self.advance()
            right = self._parse_binary(prec + 1)
            left = ast.BinaryOp(op, left, right, left.line, left.col)
        return left

    def _parse_unary(self):
        t = self.cur
        if t.type == 'OP' and t.value in ('-', '~'):
            self.advance()
            operand = self._parse_unary()
            return ast.UnaryOp(t.value, operand, t.line, t.col)
        if t.type == 'KEYWORD' and t.value == 'not':
            self.advance()
            operand = self._parse_unary()
            return ast.UnaryOp('not', operand, t.line, t.col)
        return self._parse_postfix()

    def _parse_postfix(self):
        node = self._parse_primary()
        while True:
            t = self.cur
            if t.type == 'PUNCT' and t.value == '(':
                self.advance()
                args = []
                if not (self.cur.type == 'PUNCT' and self.cur.value == ')'):
                    args.append(self.parse_expr())
                    while self.match('PUNCT', ','):
                        args.append(self.parse_expr())
                self.expect('PUNCT', ')')
                node = ast.CallExpr(node, args, node.line, node.col)
            elif t.type == 'PUNCT' and t.value == '[':
                self.advance()
                idx = self.parse_expr()
                self.expect('PUNCT', ']')
                node = ast.IndexExpr(node, idx, node.line, node.col)
            elif t.type == 'PUNCT' and t.value == '.':
                self.advance()
                attr = self.expect('IDENT').value
                node = ast.AttrExpr(node, attr, node.line, node.col)
            else:
                break
        return node

    def _parse_primary(self):
        t = self.cur

        if t.type == 'INT':
            self.advance()
            return ast.IntLit(t.value, t.line, t.col)
        if t.type == 'FLOAT':
            self.advance()
            return ast.FloatLit(t.value, t.line, t.col)
        if t.type == 'STRING':
            self.advance()
            return ast.StringLit(t.value, t.line, t.col)

        if t.type == 'KEYWORD':
            if t.value == 'true':
                self.advance()
                return ast.BoolLit(True, t.line, t.col)
            if t.value == 'false':
                self.advance()
                return ast.BoolLit(False, t.line, t.col)
            if t.value == 'none':
                self.advance()
                return ast.NoneLit(t.line, t.col)

        if t.type == 'IDENT':
            self.advance()
            return ast.Identifier(t.value, t.line, t.col)

        if t.type == 'PUNCT' and t.value == '(':
            self.advance()
            e = self.parse_expr()
            self.expect('PUNCT', ')')
            return e

        if t.type == 'PUNCT' and t.value == '[':
            self.advance()
            items = []
            if not (self.cur.type == 'PUNCT' and self.cur.value == ']'):
                items.append(self.parse_expr())
                while self.match('PUNCT', ','):
                    if self.cur.type == 'PUNCT' and self.cur.value == ']':
                        break
                    items.append(self.parse_expr())
            self.expect('PUNCT', ']')
            return ast.ArrayLit(items, t.line, t.col)

        if t.type == 'PUNCT' and t.value == '{':
            self.advance()
            pairs = []
            if not (self.cur.type == 'PUNCT' and self.cur.value == '}'):
                k = self.parse_expr()
                self.expect('PUNCT', ':')
                v = self.parse_expr()
                pairs.append((k, v))
                while self.match('PUNCT', ','):
                    if self.cur.type == 'PUNCT' and self.cur.value == '}':
                        break
                    k = self.parse_expr()
                    self.expect('PUNCT', ':')
                    v = self.parse_expr()
                    pairs.append((k, v))
            self.expect('PUNCT', '}')
            return ast.DictLit(pairs, t.line, t.col)

        raise ParseError(
            f"unexpected {t.type} {t.value!r} at {t.line}:{t.col}"
        )


def parse(source):
    from lexer import Lexer
    return Parser(Lexer(source).tokenize()).parse_program()
