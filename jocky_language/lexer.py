from dataclasses import dataclass

KEYWORDS = {
    'and', 'break', 'continue', 'elif', 'else', 'false', 'fn', 'for',
    'if', 'in', 'let', 'none', 'not', 'or', 'return', 'true', 'while',
}

TWO_CHAR_OPS = {'==', '!=', '<=', '>=', '+=', '-=', '*=', '/=', '%='}
ONE_CHAR_OPS = set('+-*/%<>|&^~')
PUNCT = set('.,:;()[]{}=')

class LexerError(Exception):
    pass

@dataclass
class Token:
    type: str
    value: object
    line: int
    col: int
    def __repr__(self):
        return f"Token({self.type}, {self.value!r}, {self.line}:{self.col})"
    def __getitem__(self, i):
        return (self.type, self.value, self.line, self.col)[i]

class Lexer:
    def __init__(self, source):
        self.src = source
        self.i = 0
        self.line = 1
        self.col = 1

    def tokenize(self):
        out = []
        while self.i < len(self.src):
            c = self.src[self.i]
            if c == '#':
                while self.i < len(self.src) and self.src[self.i] != '\n':
                    self.i += 1; self.col += 1
                continue
            if c in ' \t\r':
                self.i += 1; self.col += 1; continue
            if c == '\n':
                self.i += 1; self.line += 1; self.col = 1; continue
            if c.isdigit():
                out.append(self._number()); continue
            if c.isalpha() or c == '_':
                out.append(self._word()); continue
            if c in ('"', "'"):
                out.append(self._string(c)); continue
            two = self.src[self.i:self.i+2]
            if two in TWO_CHAR_OPS:
                out.append(Token('OP', two, self.line, self.col))
                self.i += 2; self.col += 2; continue
            if c in ONE_CHAR_OPS:
                out.append(Token('OP', c, self.line, self.col))
                self.i += 1; self.col += 1; continue
            if c in PUNCT:
                out.append(Token('PUNCT', c, self.line, self.col))
                self.i += 1; self.col += 1; continue
            raise LexerError(f"unexpected char {c!r} at {self.line}:{self.col}")
        out.append(Token('EOF', None, self.line, self.col))
        return out

    def _number(self):
        line, col = self.line, self.col
        buf = []
        while self.i < len(self.src) and self.src[self.i].isdigit():
            buf.append(self.src[self.i]); self.i += 1; self.col += 1
        if self.i < len(self.src) and self.src[self.i] == '.':
            buf.append('.'); self.i += 1; self.col += 1
            while self.i < len(self.src) and self.src[self.i].isdigit():
                buf.append(self.src[self.i]); self.i += 1; self.col += 1
            return Token('FLOAT', float(''.join(buf)), line, col)
        return Token('INT', int(''.join(buf)), line, col)

    def _word(self):
        line, col = self.line, self.col
        start = self.i
        while self.i < len(self.src) and (self.src[self.i].isalnum() or self.src[self.i] == '_'):
            self.i += 1; self.col += 1
        word = self.src[start:self.i]
        if word == 'true':
            return Token('BOOL', True, line, col)
        if word == 'false':
            return Token('BOOL', False, line, col)
        if word == 'none':
            return Token('NONE', None, line, col)
        if word in KEYWORDS:
            return Token('KEYWORD', word, line, col)
        return Token('IDENT', word, line, col)

    def _string(self, quote):
        line, col = self.line, self.col
        self.i += 1; self.col += 1
        buf = []
        while self.i < len(self.src) and self.src[self.i] != quote:
            c = self.src[self.i]
            if c == '\\' and self.i + 1 < len(self.src):
                self.i += 1; self.col += 1
                e = self.src[self.i]
                buf.append({'n':'\n','t':'\t','r':'\r','\\':'\\','"':'"',"'":"'",'0':'\0'}.get(e, e))
            else:
                buf.append(c)
            if c == '\n':
                self.line += 1; self.col = 1
            else:
                self.col += 1
            self.i += 1
        if self.i >= len(self.src):
            raise LexerError(f"unterminated string at {line}:{col}")
        self.i += 1; self.col += 1
        return Token('STR', ''.join(buf), line, col)
