"""Jockey language parser — builds tuple-based AST from tokens."""
import ast_nodes as ast
from lexer import Token, Lexer, LexerError


class ParseError(Exception):
    pass


BINARY_PREC = {
    'or':  1,
    'and': 2,
    '==':  4, '!=': 4, '<': 4, '>': 4, '<=': 4, '>=': 4,
    '|':   5, '^':   6, '&':   7,
    '<<':  8, '>>': 8,
    '+':   9, '-': 9,
    '*':  10, '/': 10, '%': 10,
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

    # ---------- program ----------

    def parse_program(self):
        body = []
        while self.cur.type != 'EOF':
            body.append(self.parse_stmt())
        return body

    # ---------- statements ----------

    def parse_stmt(self):
        t = self.cur

        if t.type == 'PUNCT' and t.value == '{':
            body = self._parse_braced_body()
            return ('block', body)

        if t.type == 'KEYWORD':
            if t.value == 'let':
                return self.parse_let()
            if t.value == 'fn':
                return self.parse_fn()
            if t.value == 'if':
                return self.parse_if()
            if t.value == 'while':
                return self.parse_while()
            if t.value == 'for':
                return self.parse_for()
            if t.value == 'return':
                return self.parse_return()
            if t.value == 'break':
                self.advance()
                return ('break',)
            if t.value == 'continue':
                self.advance()
                return ('continue',)

        expr = self.parse_expr()

        # Assignment
        if self.cur.type in ('OP', 'PUNCT') and self.cur.value in ASSIGN_OPS:
            op = self.advance().value
            val = self.parse_expr()
            if not (isinstance(expr, tuple) and expr[0] in ('ident', 'index', 'attr')):
                raise ParseError(
                    f"invalid assignment target at {t.line}:{t.col}"
                )
            return ('assign', op, expr, val)

        return ('expr', expr)

    def _parse_braced_body(self):
        self.expect('PUNCT', '{')
        body = []
        while not (self.cur.type == 'PUNCT' and self.cur.value == '}'):
            if self.cur.type == 'EOF':
                raise ParseError("unexpected EOF inside block")
            body.append(self.parse_stmt())
        self.expect('PUNCT', '}')
        return body

    def parse_let(self):
        t = self.expect('KEYWORD', 'let')
        name = self.expect('IDENT').value
        value = None
        if self.cur.type in ('OP', 'PUNCT') and self.cur.value == '=':
            self.advance()
            value = self.parse_expr()
        return ('let', name, value)

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
        return ('fn', name, params, body)

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
        return ('if', cond, then_body, elif_parts, else_body)

    def parse_while(self):
        t = self.expect('KEYWORD', 'while')
        cond = self.parse_expr()
        body = self._parse_braced_body()
        return ('while', cond, body)

    def parse_for(self):
        t = self.expect('KEYWORD', 'for')
        var = self.expect('IDENT').value
        self.expect('KEYWORD', 'in')
        iterable = self.parse_expr()
        body = self._parse_braced_body()
        return ('for', var, iterable, body)

    def parse_return(self):
        t = self.expect('KEYWORD', 'return')
        value = None
        if self.cur.type not in ('PUNCT', 'KEYWORD', 'EOF') or self.cur.value not in ('}', ';', None):
            if not (self.cur.type == 'KEYWORD' and self.cur.value in ('else', 'elif')):
                value = self.parse_expr()
        return ('return', value)

    # ---------- expressions ----------

    def parse_expr(self):
        return self._parse_binary(1)

    def _parse_binary(self, min_prec):
        left = self._parse_unary()
        while True:
            t = self.cur
            op_prec = None
            if t.type == 'OP' and t.value in BINARY_PREC:
                op_prec = BINARY_PREC[t.value]
            elif t.type == 'KEYWORD' and t.value in ('or', 'and'):
                op_prec = BINARY_PREC[t.value]
            if op_prec is None or op_prec < min_prec:
                break
            op = self.advance().value
            right = self._parse_binary(op_prec + 1)
            left = ('binop', op, left, right)
        return left

    def _parse_unary(self):
        t = self.cur
        if (t.type == 'OP' and t.value == '!') or (t.type == 'KEYWORD' and t.value == 'not'):
            op = t.value
            self.advance()
            return ('unary', op, self._parse_unary())
        if t.type == 'OP' and t.value == '-':
            self.advance()
            return ('unary', '-', self._parse_unary())
        if t.type == 'OP' and t.value == '~':
            self.advance()
            return ('unary', '~', self._parse_unary())
        return self._parse_postfix()

    def _parse_postfix(self):
        node = self._parse_primary()
        while True:
            t = self.cur
            if t.type == 'PUNCT' and t.value == '.':
                self.advance()
                prop = self.expect('IDENT').value
                node = ('attr', node, prop)
            elif t.type == 'PUNCT' and t.value == '(':
                self.advance()
                args = []
                if not (self.cur.type == 'PUNCT' and self.cur.value == ')'):
                    args.append(self.parse_expr())
                    while self.match('PUNCT', ','):
                        if self.cur.type == 'PUNCT' and self.cur.value == ')':
                            break
                        args.append(self.parse_expr())
                self.expect('PUNCT', ')')
                if isinstance(node, tuple) and node[0] == 'ident':
                    node = ('call', node[1], args)
                else:
                    node = ('call_expr', node, args)
            elif t.type == 'PUNCT' and t.value == '[':
                self.advance()
                idx = self.parse_expr()
                self.expect('PUNCT', ']')
                node = ('index', node, idx)
            else:
                break
        return node

    def _parse_primary(self):
        t = self.cur

        if t.type == 'INT':
            self.advance()
            return ('int', t.value)

        if t.type == 'FLOAT':
            self.advance()
            return ('float', t.value)

        if t.type == 'STR':
            self.advance()
            return ('str', t.value)

        if t.type == 'BOOL':
            self.advance()
            return ('bool', t.value)

        if t.type == 'NONE':
            self.advance()
            return ('none',)

        if t.type == 'IDENT':
            self.advance()
            return ('ident', t.value)

        if t.type == 'PUNCT':
            if t.value == '(':
                self.advance()
                expr = self.parse_expr()
                self.expect('PUNCT', ')')
                return expr
            if t.value == '[':
                self.advance()
                items = []
                if not (self.cur.type == 'PUNCT' and self.cur.value == ']'):
                    items.append(self.parse_expr())
                    while self.match('PUNCT', ','):
                        if self.cur.type == 'PUNCT' and self.cur.value == ']':
                            break
                        items.append(self.parse_expr())
                self.expect('PUNCT', ']')
                return ('list', items)
            if t.value == '{':
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
                return ('dict', pairs)

        raise ParseError(
            f"unexpected {t.type} {t.value!r} at {t.line}:{t.col}"
        )


def parse(source):
    from lexer import Lexer
    return Parser(Lexer(source).tokenize()).parse_program()
