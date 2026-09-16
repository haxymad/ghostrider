class Node:
    def __init__(self, line=0, col=0):
        self.line, self.col = line, col

class Program(Node):
    def __init__(self, body):
        super().__init__()
        self.body = body

class LetStmt(Node):
    def __init__(self, name, value, line=0, col=0):
        super().__init__(line, col)
        self.name, self.value = name, value

class AssignStmt(Node):
    def __init__(self, target, op, value, line=0, col=0):
        super().__init__(line, col)
        self.target, self.op, self.value = target, op, value

class ExprStmt(Node):
    def __init__(self, expr, line=0, col=0):
        super().__init__(line, col)
        self.expr = expr

class IfStmt(Node):
    def __init__(self, condition, then_body, elif_parts, else_body, line=0, col=0):
        super().__init__(line, col)
        self.condition, self.then_body = condition, then_body
        self.elif_parts, self.else_body = elif_parts or [], else_body or []

class WhileStmt(Node):
    def __init__(self, condition, body, line=0, col=0):
        super().__init__(line, col)
        self.condition, self.body = condition, body

class ForStmt(Node):
    def __init__(self, var_name, iterable, body, line=0, col=0):
        super().__init__(line, col)
        self.var_name, self.iterable, self.body = var_name, iterable, body

class FnDef(Node):
    def __init__(self, name, params, body, line=0, col=0):
        super().__init__(line, col)
        self.name, self.params, self.body = name, params, body

class ReturnStmt(Node):
    def __init__(self, value, line=0, col=0):
        super().__init__(line, col)
        self.value = value

class BreakStmt(Node):
    pass

class ContinueStmt(Node):
    pass

class Expr(Node):
    pass

class IntLit(Expr):
    def __init__(self, value, line=0, col=0):
        super().__init__(line, col)
        self.value = value

class FloatLit(Expr):
    def __init__(self, value, line=0, col=0):
        super().__init__(line, col)
        self.value = value

class StringLit(Expr):
    def __init__(self, value, line=0, col=0):
        super().__init__(line, col)
        self.value = value

class BoolLit(Expr):
    def __init__(self, value, line=0, col=0):
        super().__init__(line, col)
        self.value = value

class NoneLit(Expr):
    pass

class Identifier(Expr):
    def __init__(self, name, line=0, col=0):
        super().__init__(line, col)
        self.name = name

class BinaryOp(Expr):
    def __init__(self, op, left, right, line=0, col=0):
        super().__init__(line, col)
        self.op, self.left, self.right = op, left, right

class UnaryOp(Expr):
    def __init__(self, op, operand, line=0, col=0):
        super().__init__(line, col)
        self.op, self.operand = op, operand

class CallExpr(Expr):
    def __init__(self, func, args, line=0, col=0):
        super().__init__(line, col)
        self.func, self.args = func, args

class IndexExpr(Expr):
    def __init__(self, obj, index, line=0, col=0):
        super().__init__(line, col)
        self.obj, self.index = obj, index

class AttrExpr(Expr):
    def __init__(self, obj, attr, line=0, col=0):
        super().__init__(line, col)
        self.obj, self.attr = obj, attr

class ArrayLit(Expr):
    def __init__(self, items, line=0, col=0):
        super().__init__(line, col)
        self.items = items

class DictLit(Expr):
    def __init__(self, pairs, line=0, col=0):
        super().__init__(line, col)
        self.pairs = pairs
