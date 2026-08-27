# Generates the golden byte vectors for CnbGoldenVectorTests.cpp from the FORMAT SPECIFICATION,
# independently of the C++ writer -- that independence is the whole point: a golden file produced
# by the implementation under test proves only that the implementation is self-consistent.
import struct

POLY = 0x82F63B78
_tab = []
for i in range(256):
    c = i
    for _ in range(8):
        c = (c >> 1) ^ POLY if c & 1 else c >> 1
    _tab.append(c)

def crc32c(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc = _tab[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return (~crc) & 0xFFFFFFFF

def u16(v): return struct.pack('<H', v)
def u32(v): return struct.pack('<I', v)
def u64(v): return struct.pack('<Q', v)
def f32(v): return struct.pack('<f', v)
def f64(v): return struct.pack('<d', v)
def cstr(s):
    b = s.encode('utf-8')
    return u32(len(b)) + b
def fourcc(s): return u32(s[0] | (s[1] << 8) | (s[2] << 16) | (s[3] << 24))

HEADER_SIZE = 64
TOC_ENTRY = 48
MANDATORY = 1

def build(asset_type_id, schema_version, chunks):
    """chunks: list of (fourcc-str, flags, alignment, payload bytes)"""
    n = len(chunks)
    toc_offset = HEADER_SIZE
    cursor = toc_offset + TOC_ENTRY * n
    offsets = []
    for (_, _, align, payload) in chunks:
        rem = cursor % align
        if rem:
            cursor += align - rem
        offsets.append(cursor)
        cursor += len(payload)
    file_size = cursor

    toc = b''
    for i, (fcc, flags, align, payload) in enumerate(chunks):
        toc += fourcc(fcc.encode('ascii'))
        toc += u32(flags)
        toc += u64(offsets[i])
        toc += u64(len(payload))
        toc += u64(len(payload))
        toc += u32(crc32c(payload))
        toc += u32(0)          # compression = none
        toc += u32(align)
        toc += u32(0)          # reserved
    assert len(toc) == TOC_ENTRY * n

    out = bytearray(file_size)
    header = b'CNB\x1a' + u16(1) + u16(0) + u32(0) + u32(asset_type_id) + u32(schema_version)
    header += u32(n) + u64(file_size) + u64(toc_offset)
    assert len(header) == 40
    out[0:40] = header
    out[40:44] = u32(crc32c(toc))
    out[44:48] = u32(crc32c(bytes(out[0:44])))
    # bytes 48..64 stay zero (reserved)
    out[toc_offset:toc_offset + len(toc)] = toc
    for i, (_, _, _, payload) in enumerate(chunks):
        out[offsets[i]:offsets[i] + len(payload)] = payload
    return bytes(out)

def emit(name, data, note):
    print('    // %s' % note)
    print('    const std::vector<std::uint8_t> %s = {' % name)
    for i in range(0, len(data), 12):
        row = ', '.join('0x%02X' % b for b in data[i:i + 12])
        print('        %s,' % row)
    print('    };  // %d bytes' % len(data))
    print()

# --- Curve: preLoop=Cycle(1), postLoop=Oscillate(3), two keys -------------------------------
cmet = u32(0) + cstr('Microsoft.Xna.Framework.Curve') + cstr('golden/curve')
crvh = u32(1) + u32(3) + u32(2)
crvk = (f32(0.0) + f32(1.0) + f32(0.25) + f32(-0.25) + u32(0) +
        f32(2.0) + f32(-3.0) + f32(0.0) + f32(0.0) + u32(1))
curve = build(7, 1, [('CMET', 0, 4, cmet), ('CRVH', MANDATORY, 4, crvh), ('CRVK', MANDATORY, 4, crvk)])
emit('kGoldenCurve', curve, 'Curve schema 1: Cycle/Oscillate loop, two keys (see the decoded expectations below).')

# --- AnimationClip: 1.5s, SceneNode(1), one track of two keys --------------------------------
cmet2 = u32(0) + cstr('Microsoft.Xna.Framework.Graphics.AnimationClipEXT') + cstr('golden/clip')
aclh = f64(1.5) + u32(1) + u32(1) + u32(2)
aclt = struct.pack('<i', 4) + u32(0) + u32(2)
def key(t, tx, ty, tz, rx, ry, rz, rw, sx, sy, sz):
    return f64(t) + f32(tx) + f32(ty) + f32(tz) + f32(rx) + f32(ry) + f32(rz) + f32(rw) + f32(sx) + f32(sy) + f32(sz)
aclk = key(0.0, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0) + \
       key(1.5, 4.0, 5.0, 6.0, 0.0, 0.0, 0.0, 1.0, 2.0, 2.0, 2.0)
clip = build(6, 1, [('CMET', 0, 4, cmet2), ('ACLH', MANDATORY, 8, aclh),
                    ('ACLT', MANDATORY, 4, aclt), ('ACLK', MANDATORY, 8, aclk)])
emit('kGoldenAnimationClip', clip, 'AnimationClip schema 1: 1.5s SceneNode clip, one track on bone 4, two keys.')
