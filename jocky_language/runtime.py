"""Jockey language runtime — executes bytecode in pure Python."""
import os
import sys
import time
import math
import random
import hashlib
import base64
import uuid
import platform
import subprocess
import urllib.parse


OP = {
    'PUSH_CONST': 0, 'PUSH_STR': 1, 'PUSH_INT': 2,
    'LOAD_VAR': 3, 'STORE_VAR': 4, 'POP': 5,
    'ADD': 6, 'SUB': 7, 'MUL': 8, 'DIV': 9, 'MOD': 10,
    'EQ': 11, 'NE': 12, 'LT': 13, 'GT': 14, 'LE': 15, 'GE': 16,
    'AND': 17, 'OR': 18, 'NOT': 19, 'NEG': 20, 'BIT_NOT': 21,
    'BIT_OR': 22, 'BIT_AND': 23, 'BIT_XOR': 24,
    'CALL': 25, 'LEN': 26, 'RETURN': 27, 'GET_ATTR': 28,
    'BUILD_ARRAY': 29, 'BUILD_DICT': 30,
    'JUMP': 31, 'JUMP_IF_FALSE': 32, 'JUMP_IF_TRUE': 33,
    'NOP': 34, 'INDEX': 35, 'SET_INDEX': 36,
    # metamorphic opcodes
    'PUSH_VAR': 37, 'POP_VAR': 38,
    'ADD2': 39, 'SUB2': 40, 'MUL2': 41, 'DIV2': 42, 'CMP2': 43,
    'ADD_CONST': 44, 'SUB_CONST': 45,
    'LOAD_CONST': 46, 'CALL_BUILTIN': 47,
}


class RuntimeError_(Exception):
    pass


def c_mod(a, b):
    """C-style modulo. Sign follows the left operand.

    -7 %  3 -> -1
     7 % -3 ->  1
    -7 % -3 -> -1
    """
    if isinstance(a, bool) or isinstance(b, bool):
        raise RuntimeError_("modulo requires numeric operands")
    if isinstance(a, int) and isinstance(b, int):
        if b == 0:
            raise RuntimeError_("modulo by zero")
        q = abs(a) // abs(b)
        if (a < 0) ^ (b < 0):
            q = -q
        return a - q * b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        if b == 0:
            raise RuntimeError_("modulo by zero")
        return math.fmod(a, b)
    raise RuntimeError_("modulo requires numeric operands")


class Frame:
    __slots__ = ('code', 'pc', 'locals')
    def __init__(self, code, pc, locals_):
        self.code = code
        self.pc = pc
        self.locals = locals_


class JockeyVM:
    def __init__(self, program):
        self.prog = program
        self.consts = program.get('constants', [])
        self.var_names = program.get('var_names', [])
        self.functions = program.get('functions', {})
        self.main = program.get('main', {}).get('code', [])

        self.stack = []
        self.globals = {}
        self.call_stack = []
        self.pc = 0
        self.code = self.main
        self.locals = self.globals

        self.builtins = self._make_builtins()
        extra = program.get('_extra_builtins', {})
        if extra:
            self.builtins.update(extra)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    def _truthy(self, v):
        if v is None:
            return False
        if isinstance(v, bool):
            return v
        if isinstance(v, (int, float)):
            return v != 0
        if isinstance(v, str):
            return len(v) > 0
        if isinstance(v, (list, dict, bytes, bytearray)):
            return len(v) > 0
        return True

    def _to_str(self, v):
        if v is None:
            return 'none'
        if v is True:
            return 'true'
        if v is False:
            return 'false'
        if isinstance(v, float):
            if v.is_integer() and abs(v) < 1e16:
                return f"{v:.1f}"
            return repr(v)
        if isinstance(v, (bytes, bytearray)):
            return v.decode('utf-8', errors='replace')
        return str(v)

    def _numeric(self, v):
        # Booleans are ints at the bytecode level (PUSH_INT 0/1), so treat them
        # as numeric for all comparison / arithmetic purposes.
        return isinstance(v, (int, float))

    # ------------------------------------------------------------------
    # Entry point
    # ------------------------------------------------------------------

    def run(self):
        while 0 <= self.pc < len(self.code):
            self._step()
        return 0

    # ------------------------------------------------------------------
    # Dispatch
    # ------------------------------------------------------------------

    def _step(self):
        ins = self.code[self.pc]
        op = ins[0]
        a1 = ins[1]
        a2 = ins[2]

        if op == 0:      # PUSH_CONST
            self.stack.append(self.consts[a1])
            self.pc += 1

        elif op == 1:    # PUSH_STR (XOR decode if a2 != 0)
            s = self.consts[a1]
            if a2:
                if isinstance(s, (bytes, bytearray)):
                    s = bytes(b ^ a2 for b in s).decode('utf-8', errors='replace')
                else:
                    s = ''.join(chr(ord(c) ^ a2) for c in s)
            self.stack.append(s)
            self.pc += 1

        elif op == 2:    # PUSH_INT
            v = self.consts[a1]
            if a2:
                v = v ^ a2
            self.stack.append(v)
            self.pc += 1

        elif op == 3:    # LOAD_VAR
            name = self.var_names[a1]
            if name in self.locals:
                self.stack.append(self.locals[name])
            elif name in self.globals:
                self.stack.append(self.globals[name])
            else:
                self.stack.append(None)
            self.pc += 1

        elif op == 4:    # STORE_VAR
            name = self.var_names[a1]
            self.locals[name] = self.stack.pop()
            self.pc += 1

        elif op == 5:    # POP
            if self.stack:
                self.stack.pop()
            self.pc += 1

        elif op == 6:    # ADD
            b = self.stack.pop(); a = self.stack.pop()
            if self._numeric(a) and self._numeric(b):
                self.stack.append(a + b)
            elif isinstance(a, str) or isinstance(b, str):
                self.stack.append(self._to_str(a) + self._to_str(b))
            else:
                raise RuntimeError_(f"cannot add {type(a).__name__} and {type(b).__name__}")
            self.pc += 1

        elif op == 7:    # SUB
            b = self.stack.pop(); a = self.stack.pop()
            if not (self._numeric(a) and self._numeric(b)):
                raise RuntimeError_("subtraction requires numeric operands")
            self.stack.append(a - b)
            self.pc += 1

        elif op == 8:    # MUL
            b = self.stack.pop(); a = self.stack.pop()
            if not (self._numeric(a) and self._numeric(b)):
                raise RuntimeError_("multiplication requires numeric operands")
            self.stack.append(a * b)
            self.pc += 1

        elif op == 9:    # DIV
            b = self.stack.pop(); a = self.stack.pop()
            if not (self._numeric(a) and self._numeric(b)):
                raise RuntimeError_("division requires numeric operands")
            if b == 0:
                raise RuntimeError_("division by zero")
            self.stack.append(a / b)     # always float
            self.pc += 1

        elif op == 10:   # MOD
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(c_mod(a, b))
            self.pc += 1

        elif op == 11:   # EQ
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._eq(a, b))
            self.pc += 1

        elif op == 12:   # NE
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(0 if self._eq(a, b) else 1)
            self.pc += 1

        elif op == 13:   # LT
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._cmp(a, b, 'lt'))
            self.pc += 1

        elif op == 14:   # GT
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._cmp(a, b, 'gt'))
            self.pc += 1

        elif op == 15:   # LE
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._cmp(a, b, 'le'))
            self.pc += 1

        elif op == 16:   # GE
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._cmp(a, b, 'ge'))
            self.pc += 1

        elif op == 17:   # AND  (emitter already short-circuits; this just combines)
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(1 if (self._truthy(a) and self._truthy(b)) else 0)
            self.pc += 1

        elif op == 18:   # OR
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(1 if (self._truthy(a) or self._truthy(b)) else 0)
            self.pc += 1

        elif op == 19:   # NOT
            v = self.stack.pop()
            self.stack.append(0 if self._truthy(v) else 1)
            self.pc += 1

        elif op == 20:   # NEG
            v = self.stack.pop()
            if not self._numeric(v):
                raise RuntimeError_("unary minus requires numeric operand")
            self.stack.append(-v)
            self.pc += 1

        elif op == 21:   # BIT_NOT
            v = self.stack.pop()
            if not isinstance(v, int) or isinstance(v, bool):
                raise RuntimeError_("bitwise not requires int operand")
            self.stack.append(~v)
            self.pc += 1

        elif op == 22:   # BIT_OR
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._bitop(a, b, lambda x, y: x | y))
            self.pc += 1

        elif op == 23:   # BIT_AND
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._bitop(a, b, lambda x, y: x & y))
            self.pc += 1

        elif op == 24:   # BIT_XOR
            b = self.stack.pop(); a = self.stack.pop()
            self.stack.append(self._bitop(a, b, lambda x, y: x ^ y))
            self.pc += 1

        elif op == 25:   # CALL
            self._op_call(a1, a2)

        elif op == 26:   # LEN
            v = self.stack.pop()
            if isinstance(v, (str, list, dict, bytes, bytearray)):
                self.stack.append(len(v))
            else:
                self.stack.append(0)
            self.pc += 1

        elif op == 27:   # RETURN
            if self.call_stack:
                f = self.call_stack.pop()
                self.code = f.code
                self.pc = f.pc
                self.locals = f.locals
            else:
                self.pc = len(self.code)

        elif op == 28:   # GET_ATTR (name stored in var_names)
            v = self.stack.pop()
            name = self.var_names[a1]
            if isinstance(v, dict):
                self.stack.append(v.get(name))
            else:
                self.stack.append(None)
            self.pc += 1

        elif op == 29:   # BUILD_ARRAY
            n = a1
            if n:
                items = self.stack[-n:]
                del self.stack[-n:]
            else:
                items = []
            self.stack.append(list(items))
            self.pc += 1

        elif op == 30:   # BUILD_DICT
            n = a1
            d = {}
            for _ in range(n):
                v = self.stack.pop()
                k = self.stack.pop()
                if not isinstance(k, str):
                    raise RuntimeError_("dict keys must be strings")
                d[k] = v
            self.stack.append(d)
            self.pc += 1

        elif op == 31:   # JUMP
            self.pc = a1

        elif op == 32:   # JUMP_IF_FALSE
            c = self.stack.pop()
            self.pc = a1 if not self._truthy(c) else self.pc + 1

        elif op == 33:   # JUMP_IF_TRUE
            c = self.stack.pop()
            self.pc = a1 if self._truthy(c) else self.pc + 1

        elif op == 34:   # NOP
            self.pc += 1

        elif op == 35:   # INDEX
            idx = self.stack.pop()
            obj = self.stack.pop()
            self.stack.append(self._index(obj, idx))
            self.pc += 1

        elif op == 36:   # SET_INDEX
            val = self.stack.pop()
            idx = self.stack.pop()
            obj = self.stack.pop()
            self._set_index(obj, idx, val)
            self.pc += 1

        # ── metamorphic opcodes ─────────────────────────────────────────
        elif op == 37:   # PUSH_VAR
            self.stack.append(self.vars.get(self.code[self.pc][1], 0))
            self.pc += 1
        elif op == 38:   # POP_VAR
            self.vars[self.code[self.pc][1]] = self.stack.pop()
            self.pc += 1
        elif op == 39:   # ADD2 (pop two vars, push sum)
            self.stack.append(self.vars[self.code[self.pc][1]] + self.vars[self.code[self.pc][2]])
            self.pc += 1
        elif op == 40:   # SUB2
            self.stack.append(self.vars[self.code[self.pc][1]] - self.vars[self.code[self.pc][2]])
            self.pc += 1
        elif op == 41:   # MUL2
            self.stack.append(self.vars[self.code[self.pc][1]] * self.vars[self.code[self.pc][2]])
            self.pc += 1
        elif op == 42:   # DIV2
            self.stack.append(self.vars[self.code[self.pc][1]] / self.vars[self.code[self.pc][2]])
            self.pc += 1
        elif op == 43:   # CMP2 (comparison, pushes 1 or 0)
            a = self.vars[self.code[self.pc][1]]
            b = self.vars[self.code[self.pc][2]]
            self.stack.append(1 if a == b else 0)
            self.pc += 1
        elif op == 44:   # ADD_CONST (add constant to top of stack)
            self.stack[-1] = self.stack[-1] + self.code[self.pc][1]
            self.pc += 1
        elif op == 45:   # SUB_CONST
            self.stack[-1] = self.stack[-1] - self.code[self.pc][1]
            self.pc += 1
        elif op == 46:   # LOAD_CONST (encrypted constant onto stack)
            self.stack.append(self.constants[self.code[self.pc][1]])
            self.pc += 1
        elif op == 47:   # CALL_BUILTIN (name, arity)
            arity = self.code[self.pc][2]
            args = list(reversed([self.stack.pop() for _ in range(arity)]))
            func_name = self.code[self.pc][1]
            if func_name in self.builtins:
                result = self.builtins[func_name](*args)
                self.stack.append(result)
            else:
                raise RuntimeError_(f"unknown builtin: {func_name}")
            self.pc += 1

        else:
            raise RuntimeError_(f"unknown opcode {op} at pc {self.pc}")

    # ------------------------------------------------------------------
    # Opcode helpers
    # ------------------------------------------------------------------

    def _eq(self, a, b):
        """Returns 1 for equal, 0 for not equal. Both ints, never Python bool."""
        if self._numeric(a) and self._numeric(b):
            return 1 if a == b else 0
        if a is None or b is None:
            return 1 if a is b else 0
        if isinstance(a, str) and isinstance(b, str):
            return 1 if a == b else 0
        if isinstance(a, (list, dict, bytes, bytearray)) or \
           isinstance(b, (list, dict, bytes, bytearray)):
            return 1 if a is b else 0
        return 0

    def _cmp(self, a, b, kind):
        """Returns 1 or 0 (Python ints), never Python bool."""
        if self._numeric(a) and self._numeric(b):
            if kind == 'lt': return 1 if a < b else 0
            if kind == 'gt': return 1 if a > b else 0
            if kind == 'le': return 1 if a <= b else 0
            if kind == 'ge': return 1 if a >= b else 0
        if isinstance(a, str) and isinstance(b, str):
            if kind == 'lt': return 1 if a < b else 0
            if kind == 'gt': return 1 if a > b else 0
            if kind == 'le': return 1 if a <= b else 0
            if kind == 'ge': return 1 if a >= b else 0
        raise RuntimeError_(
            f"cannot compare {type(a).__name__} and {type(b).__name__}"
        )

    def _bitop(self, a, b, fn):
        if isinstance(a, bool) or isinstance(b, bool):
            raise RuntimeError_("bitwise operators require int operands")
        if isinstance(a, int) and isinstance(b, int):
            return fn(a, b)
        raise RuntimeError_("bitwise operators require int operands")

    def _index(self, obj, idx):
        if isinstance(obj, list) and isinstance(idx, int) and not isinstance(idx, bool):
            return obj[idx] if 0 <= idx < len(obj) else None
        if isinstance(obj, str) and isinstance(idx, int) and not isinstance(idx, bool):
            return obj[idx] if 0 <= idx < len(obj) else None
        if isinstance(obj, (bytes, bytearray)) and isinstance(idx, int) and not isinstance(idx, bool):
            return obj[idx] if 0 <= idx < len(obj) else None
        if isinstance(obj, dict) and isinstance(idx, str):
            return obj.get(idx)
        return None

    def _set_index(self, obj, idx, val):
        if isinstance(obj, list) and isinstance(idx, int) and not isinstance(idx, bool):
            if 0 <= idx < len(obj):
                obj[idx] = val
            return
        if isinstance(obj, dict) and isinstance(idx, str):
            obj[idx] = val
            return
        raise RuntimeError_(f"cannot assign into {type(obj).__name__}")

    # ------------------------------------------------------------------
    # CALL
    # ------------------------------------------------------------------

    def _op_call(self, a1, a2):
        name = self.var_names[a1]
        args = [self.stack.pop() for _ in range(a2)][::-1]

        if name in self.builtins:
            self.stack.append(self.builtins[name](*args))
            self.pc += 1
            return

        if name in self.functions:
            fn = self.functions[name]
            params = fn['params']
            if len(args) != len(params):
                raise RuntimeError_(
                    f"{name}() takes {len(params)} arguments, got {len(args)}"
                )
            new_locals = {}
            for p, v in zip(params, args):
                new_locals[p] = v
            self.call_stack.append(Frame(self.code, self.pc + 1, self.locals))
            self.code = fn['code']
            self.locals = new_locals
            self.pc = 0
            return

        raise RuntimeError_(f"undefined function '{name}'")

    # ------------------------------------------------------------------
    # Builtins
    # ------------------------------------------------------------------

    def _make_builtins(self):
        B = {}

        def b_print(*args):
            print(' '.join(self._to_str(a) for a in args))
            return None
        B['print'] = b_print

        def b_len(x):
            if isinstance(x, (str, list, dict, bytes, bytearray)):
                return len(x)
            return 0
        B['len'] = b_len

        B['str'] = lambda x: self._to_str(x)

        def b_int(x):
            if isinstance(x, bool):
                return 1 if x else 0
            if isinstance(x, int):
                return x
            if isinstance(x, float):
                return int(x)
            if isinstance(x, str):
                s = x.strip()
                if s == '':
                    return 0
                try:
                    return int(s)
                except ValueError:
                    try:
                        return int(float(s))
                    except ValueError:
                        raise RuntimeError_(f"cannot convert {x!r} to int")
            raise RuntimeError_(f"cannot convert {type(x).__name__} to int")
        B['int'] = b_int

        def b_float(x):
            if isinstance(x, bool):
                return 1.0 if x else 0.0
            if isinstance(x, (int, float)):
                return float(x)
            if isinstance(x, str):
                s = x.strip()
                if s == '':
                    return 0.0
                try:
                    return float(s)
                except ValueError:
                    raise RuntimeError_(f"cannot convert {x!r} to float")
            raise RuntimeError_(f"cannot convert {type(x).__name__} to float")
        B['float'] = b_float

        B['bool'] = lambda x: 1 if self._truthy(x) else 0

        def b_list(*a):
            if len(a) == 1 and isinstance(a[0], list):
                return list(a[0])
            if len(a) == 1 and isinstance(a[0], str):
                return list(a[0])
            return list(a)
        B['list'] = b_list

        def b_dict(*a):
            if a and isinstance(a[0], dict):
                return dict(a[0])
            return {}
        B['dict'] = b_dict

        def b_range(*a):
            if len(a) == 0:
                return []
            return list(range(*[int(x) for x in a]))
        B['range'] = b_range

        def b_abs(x):
            if not self._numeric(x):
                raise RuntimeError_("abs requires numeric operand")
            return abs(x)
        B['abs'] = b_abs

        def b_min(*a):
            if len(a) == 1 and isinstance(a[0], list):
                return min(a[0])
            return min(a)
        B['min'] = b_min

        def b_max(*a):
            if len(a) == 1 and isinstance(a[0], list):
                return max(a[0])
            return max(a)
        B['max'] = b_max

        def b_type(x):
            if x is None:                            return 'none'
            if isinstance(x, bool):                  return 'bool'
            if isinstance(x, int):                   return 'int'
            if isinstance(x, float):                 return 'float'
            if isinstance(x, str):                   return 'string'
            if isinstance(x, list):                  return 'array'
            if isinstance(x, dict):                  return 'dict'
            if isinstance(x, (bytes, bytearray)):    return 'bytes'
            return 'unknown'
        B['type'] = b_type

        # ---------------- system_* ----------------

        def b_system_platform():
            if sys.platform.startswith('linux'):  return 'linux'
            if sys.platform == 'win32':           return 'windows'
            if sys.platform == 'darwin':          return 'macos'
            return 'unknown'
        B['system_platform'] = b_system_platform

        B['system_time']  = lambda: int(time.time())
        B['system_sleep'] = lambda ms: (time.sleep(ms / 1000.0), None)[1]

        def b_system_shell(cmd):
            r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
            return r.stdout
        B['system_shell'] = b_system_shell

        def b_system_env(name=None):
            if name is None:
                return dict(os.environ)
            return os.environ.get(name)
        B['system_env'] = b_system_env

        def b_system_info():
            return {
                'os':       platform.system(),
                'arch':     platform.machine(),
                'hostname': platform.node(),
                'user':     os.environ.get('USER') or os.environ.get('USERNAME', ''),
                'cpus':     os.cpu_count() or 0,
                'kernel':   platform.release(),
            }
        B['system_info'] = b_system_info

        B['system_kernel_version'] = lambda: platform.release()
        B['system_hostname']       = lambda: platform.node()
        B['system_username']       = lambda: os.environ.get('USER') or os.environ.get('USERNAME', '')

        # ---------------- fs_* ----------------

        B['fs_cwd']    = lambda: os.getcwd()
        B['fs_chdir']  = lambda p: (os.chdir(p), True)[1]

        def b_fs_list_dir(p='.'):
            return sorted(os.listdir(p))
        B['fs_list_dir'] = b_fs_list_dir

        def b_fs_read(p):
            with open(p, 'rb') as f:
                return f.read()
        B['fs_read'] = b_fs_read

        def b_fs_write(p, d):
            if isinstance(d, str):
                d = d.encode('utf-8')
            with open(p, 'wb') as f:
                f.write(d)
            return True
        B['fs_write'] = b_fs_write

        B['fs_exists'] = lambda p: os.path.exists(p)
        B['fs_size']   = lambda p: os.path.getsize(p)

        def b_fs_stat(p):
            s = os.stat(p)
            return {
                'size':  s.st_size,
                'mode':  s.st_mode,
                'mtime': int(s.st_mtime),
                'ctime': int(s.st_ctime),
                'atime': int(s.st_atime),
                'uid':   getattr(s, 'st_uid', 0),
                'gid':   getattr(s, 'st_gid', 0),
                'inode': getattr(s, 'st_ino', 0),
            }
        B['fs_stat'] = b_fs_stat

        def b_fs_hash(p, algo='sha256'):
            h = hashlib.new(algo)
            with open(p, 'rb') as f:
                for chunk in iter(lambda: f.read(65536), b''):
                    h.update(chunk)
            return h.hexdigest()
        B['fs_hash'] = b_fs_hash

        B['fs_hash_dir'] = lambda p: {}
        B['fs_hide']     = lambda p: False
        B['fs_unhide']   = lambda p: False

        # ---------------- process_* ----------------

        B['process_self_pid']   = lambda: os.getpid()
        B['process_parent_pid'] = lambda: os.getppid()

        def b_process_list():
            out = []
            try:
                for pid in sorted(int(x) for x in os.listdir('/proc') if x.isdigit()):
                    try:
                        with open(f'/proc/{pid}/stat') as f:
                            stat = f.read()
                        rp = stat.rfind(')')
                        lp = stat.find('(')
                        name = stat[lp + 1:rp] if rp > lp >= 0 else ''
                        out.append({'pid': pid, 'name': name})
                    except Exception:
                        pass
            except Exception:
                pass
            return out
        B['process_list'] = b_process_list

        def b_process_kill(pid, sig=None):
            try:
                os.kill(int(pid), 15 if sig is None else int(sig))
                return True
            except Exception:
                return False
        B['process_kill'] = b_process_kill

        B['process_fork']    = lambda: (os.fork() if hasattr(os, 'fork') else None)
        B['process_hide']    = lambda pid: False
        B['process_unhide']  = lambda pid: False
        B['process_elevate'] = lambda: False

        # ---------------- memory_* (stubs) ----------------

        B['memory_read']    = lambda pid, addr, n: b''
        B['memory_write']   = lambda pid, addr, d: 0
        B['memory_scan']    = lambda pid, pat, opts=None: []
        B['memory_dump']    = lambda pid, base, size: b''
        B['memory_strings'] = lambda pid, min_len=4: []
        B['memory_compare'] = lambda pid1, a1, pid2, a2, n: 0

        # ---------------- net_* ----------------

        def b_net_connect(host, port):
            import socket
            try:
                s = socket.create_connection((host, int(port)), timeout=5)
                return s.fileno()
            except Exception:
                return -1
        B['net_connect'] = b_net_connect
        B['net_send']    = lambda sock, data: 0
        B['net_recv']    = lambda sock, n: b''
        B['net_close']   = lambda sock: True

        def b_net_resolve(host):
            import socket
            try:
                return socket.gethostbyname(host)
            except Exception:
                return ''
        B['net_resolve'] = b_net_resolve

        B['net_connections'] = lambda: []
        B['net_listening']   = lambda: []
        B['net_arp']         = lambda: []

        # ---------------- cred_* ----------------

        def b_cred_users():
            out = []
            try:
                import pwd
                for u in pwd.getpwall():
                    out.append({'name': u.pw_name, 'uid': u.pw_uid, 'gid': u.pw_gid})
            except Exception:
                pass
            return out
        B['cred_users'] = b_cred_users

        B['cred_secrets']        = lambda: []
        B['cred_process_memory'] = lambda: {}
        B['cred_browser']        = lambda: []
        B['cred_wifi']           = lambda: []
        B['cred_ssh_keys']       = lambda: []
        B['cred_keyring']        = lambda: []
        B['cred_sessions']       = lambda: []

        # ---------------- kernel_* ----------------

        B['kernel_version'] = lambda: platform.release()

        def b_kernel_modules():
            out = []
            try:
                with open('/proc/modules') as f:
                    for line in f:
                        parts = line.split()
                        if parts:
                            out.append({'name': parts[0]})
            except Exception:
                pass
            return out
        B['kernel_modules'] = b_kernel_modules
        B['kernel_drivers'] = b_kernel_modules

        B['kernel_rootkit_check']    = lambda: {}
        B['kernel_syscall_check']    = lambda: {}
        B['kernel_hidden_processes'] = lambda: []
        B['kernel_hidden_modules']   = lambda: []
        B['kernel_netfilter_check']  = lambda: {}
        B['kernel_timers']           = lambda: []
        B['kernel_callbacks']        = lambda: []

        # ---------------- crypto_* ----------------

        def b_crypto_md5(d):
            if isinstance(d, str):
                d = d.encode('utf-8')
            return hashlib.md5(d).hexdigest()
        B['crypto_md5'] = b_crypto_md5

        def b_crypto_sha256(d):
            if isinstance(d, str):
                d = d.encode('utf-8')
            return hashlib.sha256(d).hexdigest()
        B['crypto_sha256'] = b_crypto_sha256

        def b_crypto_xor(data, key):
            if isinstance(key, str):
                key = key.encode('utf-8')
            elif isinstance(key, int) and not isinstance(key, bool):
                key = bytes([key & 0xff])
            if isinstance(data, str):
                data = data.encode('utf-8')
            if not isinstance(data, (bytes, bytearray)):
                raise RuntimeError_("crypto_xor: data must be bytes or string")
            k = key if key else b'\x00'
            return bytes([data[i] ^ k[i % len(k)] for i in range(len(data))])
        B['crypto_xor'] = b_crypto_xor

        def b_crypto_xor_str(enc_data, key):
            """Decrypt a XOR+rotate encrypted string. Used by polymorphic decryptors."""
            if isinstance(enc_data, (bytes, bytearray)):
                data = bytes(enc_data)
            elif isinstance(enc_data, str):
                data = enc_data.encode('utf-8')
            else:
                raise RuntimeError_("crypto_xor_str: data must be bytes or string")
            if isinstance(key, bool):
                raise RuntimeError_("crypto_xor_str: key must be int or bytes")
            if isinstance(key, int):
                key = bytes([key & 0xff])
            elif isinstance(key, str):
                key = key.encode('utf-8')
            # reverse: sub 0x33, xor, then rotate left by key bytes
            step1 = bytes(((b - 0x33) & 0xFF ^ key[i % len(key)])
                          for i, b in enumerate(data))
            # rotate left by len(key) positions
            rot = len(key) % len(step1) if step1 else 0
            result = step1[rot:] + step1[:rot] if rot else step1
            return result.decode('utf-8', errors='replace')
        B['crypto_xor_str'] = b_crypto_xor_str

        def b_crypto_decrypt_addsub(enc_data, key, rot):
            """Additive-subtractive mask decryptor."""
            if isinstance(enc_data, (bytes, bytearray)):
                data = bytes(enc_data)
            elif isinstance(enc_data, str):
                data = enc_data.encode('utf-8')
            else:
                raise RuntimeError_("crypto_decrypt_addsub: data must be bytes or string")
            if isinstance(key, bool):
                raise RuntimeError_("crypto_decrypt_addsub: key must be int")
            k = key & 0xff
            r = int(rot) & 0xFF
            # reverse the mask: sub 0x33, not, add key, sub key
            step1 = bytes(((b - 0x33) & 0xFF ^ k) for b in data)
            # rotate left by r
            rot = r % len(step1) if step1 else 0
            result = step1[rot:] + step1[:rot] if rot else step1
            return result.decode('utf-8', errors='replace')
        B['crypto_decrypt_addsub'] = b_crypto_decrypt_addsub

        def b_crypto_decrypt_twopass(enc_data, key):
            """Two-pass shift-XOR decryptor."""
            if isinstance(enc_data, (bytes, bytearray)):
                data = bytes(enc_data)
            elif isinstance(enc_data, str):
                data = enc_data.encode('utf-8')
            else:
                raise RuntimeError_("crypto_decrypt_twopass: data must be bytes or string")
            if isinstance(key, bool):
                raise RuntimeError_("crypto_decrypt_twopass: key must be int")
            k = key & 0xff
            # first pass: xor with (key << 1) & 0xFF
            step1 = bytes((b ^ ((k << 1) & 0xFF)) for b in data)
            # second pass: xor with (key >> 1) & 0xFF
            step2 = bytes((b ^ ((k >> 1) & 0xFF)) for b in step1)
            # final: sub 0x33
            step3 = bytes(((b - 0x33) & 0xFF) for b in step2)
            # rotate left by key & 0xF
            rot = k & 0xF
            rot = rot % len(step3) if step3 else 0
            result = step3[rot:] + step3[:rot] if rot else step3
            return result.decode('utf-8', errors='replace')
        B['crypto_decrypt_twopass'] = b_crypto_decrypt_twopass

        def b_crypto_b64_encode(d):
            if isinstance(d, str):
                d = d.encode('utf-8')
            return base64.b64encode(d).decode('ascii')
        B['crypto_b64_encode'] = b_crypto_b64_encode

        def b_crypto_b64_decode(d):
            if isinstance(d, str):
                d = d.encode('ascii')
            return base64.b64decode(d)
        B['crypto_b64_decode'] = b_crypto_b64_decode

        # ---------------- data_* ----------------

        def b_hex_dump(d):
            if isinstance(d, str):
                d = d.encode('utf-8')
            return ' '.join(f'{b:02x}' for b in d)
        B['data_hex_dump'] = b_hex_dump

        B['data_timestamp'] = lambda: int(time.time())
        B['data_uuid']      = lambda: str(uuid.uuid4())

        def b_random_bytes(n):
            return bytes(random.getrandbits(8) for _ in range(int(n)))
        B['data_random_bytes'] = b_random_bytes

        B['data_url_encode'] = lambda s: urllib.parse.quote(s)
        B['data_url_decode'] = lambda s: urllib.parse.unquote(s)

        # ---------------- ui_* / debug_* ----------------

        B['ui_clear'] = lambda: None
        B['ui_text']  = lambda msg: (print(self._to_str(msg)), None)[1]
        B['ui_beep']  = lambda: None
        B['ui_input'] = lambda prompt='': input(self._to_str(prompt))

        def b_debug_trace(msg):
            sys.stderr.write(self._to_str(msg) + '\n')
            return None
        B['debug_trace'] = b_debug_trace

        # ---------------- misc ----------------

        B['supports'] = lambda name: (name in B) or (name in self.functions)

        return B
