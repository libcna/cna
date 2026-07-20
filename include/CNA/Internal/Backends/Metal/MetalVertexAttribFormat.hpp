#pragma once

#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"

// plan_metal.md METAL-14: the XNA VertexElementFormat -> Metal attribute-shape table this whole
// backend's generic-declaration path (METAL-26/27) is built on. Only reads a plain XNA framework
// enum and returns a plain C++ struct -- zero Objective-C/Metal-framework dependency -- so unlike
// the real MTLVertexFormat translation this stays in the .mm (see MetalGraphicsBackend.mm's own
// one-line switch), this table can be genuinely unit-tested on any platform without an Apple
// toolchain.
namespace CNA::Internal::Backends::Metal
{
    // Neutral, platform-agnostic stand-in for MTLVertexFormat -- Apple's own enum lives in
    // <Metal/Metal.h>, unavailable on this Linux machine. Every enumerator name matches its
    // intended MTLVertexFormat target 1:1 so the .mm-side translation switch is a trivial,
    // low-risk rename, not a re-derivation.
    enum class MetalVertexAttribKind
    {
        Float1,
        Float2,
        Float3,
        Float4,
        UChar4Normalized,
        UChar4,
        Short2,
        Short4,
        Short2Normalized,
        Short4Normalized,
        Half2,
        Half4,
    };

    // componentCount is carried alongside kind for documentation/test purposes -- the kind alone
    // is sufficient to pick the real MTLVertexFormat, since each MetalVertexAttribKind value
    // already implies its own component count.
    struct MetalVertexAttribFormat
    {
        MetalVertexAttribKind kind;
        int componentCount;
    };

    inline bool operator==(const MetalVertexAttribFormat& a, const MetalVertexAttribFormat& b)
    {
        return a.kind == b.kind && a.componentCount == b.componentCount;
    }

    // Mirrors EasyGLGraphicsBackend::DescribeVertexElementFormat()'s own already-tested GL table
    // field-for-field (component count matches exactly). Metal has no separate "read as integer"
    // flag the way GL's glVertexAttribIPointer needs -- MTLVertexFormatUChar4 (unlike
    // ...Normalized) already reads as a raw, unnormalized value in the vertex shader, so Byte4's
    // raw-integer intent (XNA's BLENDINDICES-style usage) survives via the format choice itself,
    // not a separate boolean, matching this file's own existing hand-written stride-52/56/68
    // descriptors' UChar4 (not UChar4Normalized) choice for boneIndices.
    inline MetalVertexAttribFormat DescribeMetalVertexElementFormat(Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
    {
        using VEF = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        switch (format)
        {
            case VEF::Single:           return { MetalVertexAttribKind::Float1, 1 };
            case VEF::Vector2:          return { MetalVertexAttribKind::Float2, 2 };
            case VEF::Vector3:          return { MetalVertexAttribKind::Float3, 3 };
            case VEF::Vector4:          return { MetalVertexAttribKind::Float4, 4 };
            case VEF::Color:            return { MetalVertexAttribKind::UChar4Normalized, 4 };
            case VEF::Byte4:            return { MetalVertexAttribKind::UChar4, 4 };
            case VEF::Short2:           return { MetalVertexAttribKind::Short2, 2 };
            case VEF::Short4:           return { MetalVertexAttribKind::Short4, 4 };
            case VEF::NormalizedShort2: return { MetalVertexAttribKind::Short2Normalized, 2 };
            case VEF::NormalizedShort4: return { MetalVertexAttribKind::Short4Normalized, 4 };
            case VEF::HalfVector2:      return { MetalVertexAttribKind::Half2, 2 };
            case VEF::HalfVector4:      return { MetalVertexAttribKind::Half4, 4 };
        }
        return { MetalVertexAttribKind::Float3, 3 };
    }
}
