"""Serialize/deserialize Jockey bytecode to/from the .jkb binary format."""
import struct

MAGIC   = b'JKB1'
VERSION = 1

TAG_INT   = 0
TAG_STR   = 1
TAG_FLOAT = 2
TAG_NONE  = 3


def serialize_program(prog):
    buf = bytearray()
    buf += MAGIC
    buf += struct.pack('<I', VERSION)

    consts = prog.get('constants', [])
    buf += struct.pack('<I', len(consts))
    for c in consts:
        if c is None:
            buf += struct.pack('<B', TAG_NONE)
        elif isinstance(c, bool):
            buf += struct.pack('<B', TAG_INT)
            buf += struct.pack('<q', int(c))
        elif isinstance(c, int):
            buf += struct.pack('<B', TAG_INT)
            buf += struct.pack('<q', c)
        elif isinstance(c, float):
            buf += struct.pack('<B', TAG_FLOAT)
            buf += struct.pack('<d', c)
        elif isinstance(c, str):
            b = c.encode('utf-8')
            buf += struct.pack('<B', TAG_STR)
            buf += struct.pack('<I', len(b))
            buf += b
        elif isinstance(c, (bytes, bytearray)):
            buf += struct.pack('<B', TAG_STR)
            buf += struct.pack('<I', len(c))
            buf += bytes(c)
        else:
            raise TypeError(f"unsupported constant type: {type(c).__name__}")

    vnames = prog.get('var_names', [])
    buf += struct.pack('<I', len(vnames))
    for n in vnames:
        b = n.encode('utf-8')
        buf += struct.pack('<I', len(b))
        buf += b

    funcs = prog.get('functions', {})
    buf += struct.pack('<I', len(funcs))
    for name, info in funcs.items():
        b = name.encode('utf-8')
        buf += struct.pack('<I', len(b))
        buf += b
        params = info.get('params', [])
        buf += struct.pack('<I', len(params))
        for p in params:
            pb = p.encode('utf-8')
            buf += struct.pack('<I', len(pb))
            buf += pb
        code = info.get('code', [])
        buf += struct.pack('<I', len(code))
        for op, a1, a2 in code:
            buf += struct.pack('<Bii', op, a1, a2)

    main = prog.get('main', {}).get('code', [])
    buf += struct.pack('<I', len(main))
    for op, a1, a2 in main:
        buf += struct.pack('<Bii', op, a1, a2)

    return bytes(buf)


def deserialize_program(data):
    if data[:4] != MAGIC:
        raise ValueError(f"bad magic {data[:4]!r}")
    off = 8

    n = struct.unpack_from('<I', data, off)[0]
    off += 4
    consts = []
    for _ in range(n):
        tag = data[off]
        off += 1
        if tag == TAG_INT:
            v = struct.unpack_from('<q', data, off)[0]
            off += 8
        elif tag == TAG_FLOAT:
            v = struct.unpack_from('<d', data, off)[0]
            off += 8
        elif tag == TAG_NONE:
            v = None
        elif tag == TAG_STR:
            l = struct.unpack_from('<I', data, off)[0]
            off += 4
            v = data[off:off + l].decode('utf-8', errors='replace')
            off += l
        else:
            raise ValueError(f"bad const tag {tag}")
        consts.append(v)

    n = struct.unpack_from('<I', data, off)[0]
    off += 4
    vnames = []
    for _ in range(n):
        l = struct.unpack_from('<I', data, off)[0]
        off += 4
        vnames.append(data[off:off + l].decode('utf-8'))
        off += l

    n = struct.unpack_from('<I', data, off)[0]
    off += 4
    funcs = {}
    for _ in range(n):
        l = struct.unpack_from('<I', data, off)[0]
        off += 4
        name = data[off:off + l].decode('utf-8')
        off += l

        pc = struct.unpack_from('<I', data, off)[0]
        off += 4
        params = []
        for _ in range(pc):
            l = struct.unpack_from('<I', data, off)[0]
            off += 4
            params.append(data[off:off + l].decode('utf-8'))
            off += l

        cc = struct.unpack_from('<I', data, off)[0]
        off += 4
        code = []
        for _ in range(cc):
            op, a1, a2 = struct.unpack_from('<Bii', data, off)
            off += 9
            code.append([op, a1, a2])

        funcs[name] = {'params': params, 'code': code}

    mc = struct.unpack_from('<I', data, off)[0]
    off += 4
    main = []
    for _ in range(mc):
        op, a1, a2 = struct.unpack_from('<Bii', data, off)
        off += 9
        main.append([op, a1, a2])

    return {
        'constants': consts,
        'var_names': vnames,
        'functions': funcs,
        'main': {'code': main},
    }
