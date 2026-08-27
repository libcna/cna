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

# --- Model: one part, no bones/skeleton/animations/lights, one external texture ---------------
# Deliberately minimal but NOT degenerate: it exercises the string table, the 368-byte material
# record (whose defaults are the thing most likely to drift silently), the XREF table, and the
# 16-byte-aligned geometry chunks.
NO_INDEX = 0xFFFFFFFF

cmet3 = u32(0) + cstr('Microsoft.Xna.Framework.Graphics.Model') + cstr('golden/model')

# XREF: one Texture2D reference.
xref = u32(1) + u32(0) + u32(1) + cstr('Textures/wall')

# MDLH: flags(0), boneCount 0, partCount 1, meshCount 1, lightCount 0, animationCount 0
mdlh = u32(0) + u32(0) + u32(1) + u32(1) + u32(0) + u32(0)

# MSTR: interned in first-seen order, and DEDUPLICATED. The part and the mesh are both named
# "Hull" -- which is what the compiler produces, since it names a mesh after its part -- so the
# table holds ONE string and both rows index it. Getting this wrong is how the vector earned its
# place: written from the specification it was two entries, and the writer disagreed.
mstr = u32(1) + cstr('Hull')

# MMSH: one mesh row, one part row, then the slot array.
mesh_row = u32(0) + struct.pack('<i', -1) + u32(0) + u32(1)          # name 0 (shared), no parent bone
part_row = (u32(0) +          # nameIndex
            u32(0) + u32(0) + u32(NO_INDEX) +   # vertex/index/morph chunk ordinals
            u32(16) + u32(3) +                  # vertexStride, vertexCount
            u32(3) + u32(2) +                   # indexCount, indexElementSize
            u32(4) + u32(1) +                   # topology TRIANGLES, primitiveCount
            u32(0) + u32(NO_INDEX) +            # effectKind BasicEffect, no external effect
            u32(0) + u32(0))                    # materialIndex, flags
assert len(mesh_row) == 16 and len(part_row) == 56
mmsh = mesh_row + part_row + u32(1) + u32(0)

# MMAT: one material -- base colour texture is XREF 0, everything else at its documented default.
mat = u32(0) + u32(NO_INDEX) * 7                       # 8 texture refs
mat += f32(1.0) * 4                                    # baseColorFactor
mat += f32(0.0) * 3                                    # emissiveFactor
mat += f32(1.0) * 3                                    # specularColorFactor
mat += f32(1.0) + f32(1.0) + f32(1.5) + f32(1.0)       # metallic, roughness, ior, specular
mat += f32(1.0) + f32(1.0) + f32(0.5)                  # normalScale, occlusionStrength, alphaCutoff
mat += u32(0) + u32(0)                                 # alphaMode Opaque, flags
mat += bytes(7) + bytes(1)                             # texCoordSets[7] + reserved pad
mat += (f32(0.0) + f32(0.0) + f32(1.0) + f32(1.0) + f32(0.0)) * 7   # UV transforms
mat += (u32(0) + u32(0) + u32(0) + u32(0)) * 7         # sampler states
assert len(mat) == 368, len(mat)
mmat = u32(1) + mat

mvtx = bytes(range(16 * 3))
midx = struct.pack('<HHH', 0, 1, 2)

model = build(5, 1, [('CMET', 0, 4, cmet3), ('XREF', MANDATORY, 4, xref),
                     ('MDLH', MANDATORY, 4, mdlh), ('MSTR', MANDATORY, 4, mstr),
                     ('MMSH', MANDATORY, 4, mmsh), ('MMAT', MANDATORY, 4, mmat),
                     ('MVTX', MANDATORY, 16, mvtx), ('MIDX', MANDATORY, 16, midx)])
emit('kGoldenModel', model, 'Model schema 1: one boneless BasicEffect part, one external texture.')

# --- TextureCube: 2x2 faces, one mip, one Rgba8 representation --------------------------------
# A cube rather than a flat 2D texture on purpose. It is the shape that exercises everything the
# other two texture types do NOT: six payload chunks whose ORDER is load-bearing (face-major),
# the descriptor's firstPayloadOrdinal/payloadCount tiling rule, and the 16-byte payload
# alignment showing up six times rather than once. A Texture2D vector would pin a strictly
# smaller set of rules.
cmet4 = u32(0) + cstr('Microsoft.Xna.Framework.Graphics.TextureCube') + cstr('golden/cube')
# TEXH: width 2, height 2, depth 1, faceCount 6, mipCount 1, representationCount 1
texh = u32(2) + u32(2) + u32(1) + u32(6) + u32(1) + u32(1)
# TEXR: format Rgba8(1), firstPayloadOrdinal 0, payloadCount 6, flags 0, totalPayloadBytes 6*16
texr = u32(1) + u32(0) + u32(6) + u32(0) + u64(6 * 16)
# Six 2x2 Rgba8 faces, each filled with its own face index so a reordering is visible in the
# bytes themselves rather than only in a decoder's output.
face_payloads = [bytes([f * 40 + 1] * 16) for f in range(6)]
cube_chunks = [('CMET', 0, 4, cmet4), ('TEXH', MANDATORY, 4, texh), ('TEXR', MANDATORY, 4, texr)]
cube_chunks += [('TEXD', MANDATORY, 16, p) for p in face_payloads]
texturecube = build(3, 1, cube_chunks)
emit('kGoldenTextureCube', texturecube,
     'TextureCube schema 1: 2x2 faces, one mip, one Rgba8 representation, six face payloads.')
