// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-2: unit checks for D3D9's own format/state/vertex-layout mapping tables
// (D9-20/D9-21/D9-22). No device/window/GPU needed -- these are pure functions.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/DirectX9/D3D9FormatMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9StateMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9VertexDeclarations.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX9;

static int passCount = 0;
static int totalCount = 0;

static void check(bool ok, const char* label)
{
    ++totalCount;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (ok) ++passCount;
}

int main()
{
    // D9-20: format mapping.
    check(SurfaceFormatToD3D9(static_cast<int>(SurfaceFormat::Color)) == D3DFMT_A8B8G8R8,
          "SurfaceFormat::Color -> D3DFMT_A8B8G8R8 (R,G,B,A memory order)");
    check(SurfaceFormatToD3D9(static_cast<int>(SurfaceFormat::ColorBgraEXT)) == D3DFMT_A8R8G8B8,
          "SurfaceFormat::ColorBgraEXT -> D3DFMT_A8R8G8B8 (B,G,R,A memory order)");
    check(SurfaceFormatToD3D9(static_cast<int>(SurfaceFormat::Dxt5)) == D3DFMT_DXT5,
          "SurfaceFormat::Dxt5 -> D3DFMT_DXT5");
    check(SurfaceFormatToD3D9(static_cast<int>(SurfaceFormat::Rgba1010102)) == D3DFMT_A2B10G10R10,
          "SurfaceFormat::Rgba1010102 -> D3DFMT_A2B10G10R10 (NOT A2R10G10B10 -- bit layout, not name similarity)");
    check(SurfaceFormatToD3D9(static_cast<int>(SurfaceFormat::NormalizedByte2)) == D3DFMT_V8U8,
          "SurfaceFormat::NormalizedByte2 -> D3DFMT_V8U8");
    check(SurfaceFormatToD3D9(static_cast<int>(SurfaceFormat::Bc7EXT)) == D3DFMT_UNKNOWN,
          "SurfaceFormat::Bc7EXT -> D3DFMT_UNKNOWN (BC7 postdates D3D9, genuinely no equivalent)");
    check(DepthFormatToD3D9(static_cast<int>(DepthFormat::None)) == D3DFMT_UNKNOWN,
          "DepthFormat::None -> D3DFMT_UNKNOWN");
    check(DepthFormatToD3D9(static_cast<int>(DepthFormat::Depth24)) == D3DFMT_D24X8,
          "DepthFormat::Depth24 -> D3DFMT_D24X8 (D3D9 HAS a real depth-only 24-bit format, unlike D3D11)");
    check(DepthFormatToD3D9(static_cast<int>(DepthFormat::Depth24Stencil8)) == D3DFMT_D24S8,
          "DepthFormat::Depth24Stencil8 -> D3DFMT_D24S8");

    // D9-21: state mapping.
    check(BlendToD3D9(static_cast<int>(Blend::One)) == D3DBLEND_ONE, "Blend::One -> D3DBLEND_ONE");
    check(BlendToD3D9(static_cast<int>(Blend::InverseSourceAlpha)) == D3DBLEND_INVSRCALPHA,
          "Blend::InverseSourceAlpha -> D3DBLEND_INVSRCALPHA");
    check(BlendFunctionToD3D9(static_cast<int>(BlendFunction::ReverseSubtract)) == D3DBLENDOP_REVSUBTRACT,
          "BlendFunction::ReverseSubtract -> D3DBLENDOP_REVSUBTRACT");
    // REMED-GFX-089: lock the four comparison modes used by the focused live-depth controls.
    // In particular, XNA ordinal 0 is Always -> D3DCMP_ALWAYS (numeric value 8), not a
    // Less/LessEqual default; the old smoke fixture left this sticky state active.
    check(CompareFunctionToD3D9(static_cast<int>(CompareFunction::Always)) == D3DCMP_ALWAYS,
          "CompareFunction::Always -> D3DCMP_ALWAYS");
    check(CompareFunctionToD3D9(static_cast<int>(CompareFunction::Less)) == D3DCMP_LESS,
          "CompareFunction::Less -> D3DCMP_LESS");
    check(CompareFunctionToD3D9(static_cast<int>(CompareFunction::LessEqual)) == D3DCMP_LESSEQUAL,
          "CompareFunction::LessEqual -> D3DCMP_LESSEQUAL");
    check(CompareFunctionToD3D9(static_cast<int>(CompareFunction::Greater)) == D3DCMP_GREATER,
          "CompareFunction::Greater -> D3DCMP_GREATER");
    check(CullModeToD3D9(static_cast<int>(CullMode::None)) == D3DCULL_NONE,
          "CullMode::None -> D3DCULL_NONE");
    check(CullModeToD3D9(static_cast<int>(CullMode::CullClockwiseFace)) == D3DCULL_CW,
          "CullMode::CullClockwiseFace -> D3DCULL_CW (clockwise-is-front convention)");
    check(CullModeToD3D9(static_cast<int>(CullMode::CullCounterClockwiseFace)) == D3DCULL_CCW,
          "CullMode::CullCounterClockwiseFace -> D3DCULL_CCW");
    check(FillModeToD3D9(static_cast<int>(FillMode::WireFrame)) == D3DFILL_WIREFRAME,
          "FillMode::WireFrame -> D3DFILL_WIREFRAME");
    check(TextureAddressModeToD3D9(static_cast<int>(TextureAddressMode::Mirror)) == D3DTADDRESS_MIRROR,
          "TextureAddressMode::Mirror -> D3DTADDRESS_MIRROR");
    check(StencilOperationToD3D9(static_cast<int>(StencilOperation::IncrementSaturation)) == D3DSTENCILOP_INCRSAT,
          "StencilOperation::IncrementSaturation -> D3DSTENCILOP_INCRSAT (clamping, not Increment's wrapping)");
    check(StencilOperationToD3D9(static_cast<int>(StencilOperation::Increment)) == D3DSTENCILOP_INCR,
          "StencilOperation::Increment -> D3DSTENCILOP_INCR (wrapping)");

    {
        D3D9FilterTriple t = TextureFilterToD3D9(static_cast<int>(TextureFilter::MinLinearMagPointMipLinear));
        check(t.min == D3DTEXF_LINEAR && t.mag == D3DTEXF_POINT && t.mip == D3DTEXF_LINEAR,
              "TextureFilter::MinLinearMagPointMipLinear decomposes to {LINEAR,POINT,LINEAR}");
    }
    {
        D3D9FilterTriple t = TextureFilterToD3D9(static_cast<int>(TextureFilter::Anisotropic));
        check(t.min == D3DTEXF_ANISOTROPIC && t.mag == D3DTEXF_ANISOTROPIC && t.mip == D3DTEXF_LINEAR,
              "TextureFilter::Anisotropic decomposes to {ANISOTROPIC,ANISOTROPIC,LINEAR} (mip can't be ANISOTROPIC in D3D9)");
    }
    {
        D3D9FilterTriple t = TextureFilterToD3D9(static_cast<int>(TextureFilter::Point));
        check(t.min == D3DTEXF_POINT && t.mag == D3DTEXF_POINT && t.mip == D3DTEXF_POINT,
              "TextureFilter::Point decomposes to {POINT,POINT,POINT}");
    }

    // D9-22: stride-keyed vertex declarations.
    {
        UINT count = 0;
        const D3DVERTEXELEMENT9* elems = VertexElementsForStrideD3D9(16, count);
        check(elems != nullptr && count == 2 &&
              elems[0].Usage == D3DDECLUSAGE_POSITION && elems[0].Offset == 0 &&
              elems[1].Usage == D3DDECLUSAGE_COLOR && elems[1].Offset == 12 &&
              elems[1].Type == D3DDECLTYPE_UBYTE4N,
              "stride 16 -> POSITION0+COLOR0(UBYTE4N -- not D3DDECLTYPE_D3DCOLOR, D9-82's own "
              "empirically-confirmed R/B-swizzle fix), matching VertexPositionColor's own layout");
    }
    {
        UINT count = 0;
        const D3DVERTEXELEMENT9* elems = VertexElementsForStrideD3D9(24, count);
        check(elems != nullptr && count == 3 &&
              elems[1].Usage == D3DDECLUSAGE_COLOR && elems[1].Offset == 12 &&
              elems[1].Type == D3DDECLTYPE_UBYTE4N &&
              elems[2].Usage == D3DDECLUSAGE_TEXCOORD && elems[2].Offset == 16,
              "stride 24 -> POSITION0+COLOR0(UBYTE4N)+TEXCOORD0, matching VertexPositionColorTexture's own layout");
    }
    {
        // D9-82d: the D3D9-only stride-28 layout (Position+TexCoord0+TexCoord1) added for
        // DualTextureEffect's real VSInputTx2, which genuinely needs two distinct UV sets.
        UINT count = 0;
        const D3DVERTEXELEMENT9* elems = VertexElementsForStrideD3D9(28, count);
        check(elems != nullptr && count == 3 &&
              elems[0].Usage == D3DDECLUSAGE_POSITION && elems[0].Offset == 0 &&
              elems[1].Usage == D3DDECLUSAGE_TEXCOORD && elems[1].UsageIndex == 0 && elems[1].Offset == 12 &&
              elems[2].Usage == D3DDECLUSAGE_TEXCOORD && elems[2].UsageIndex == 1 && elems[2].Offset == 20,
              "stride 28 -> POSITION0+TEXCOORD0+TEXCOORD1 (D3D9-only), matching DualTextureEffect's VSInputTx2");
    }
    {
        UINT count = 0;
        const D3DVERTEXELEMENT9* elems = VertexElementsForStrideD3D9(52, count);
        check(elems != nullptr && count == 5 &&
              elems[4].Usage == D3DDECLUSAGE_BLENDINDICES && elems[4].Offset == 48 &&
              elems[4].Type == D3DDECLTYPE_UBYTE4,
              "stride 52 -> full skinned layout, matching VertexPositionNormalTextureSkinned's own layout");
        // Every array must end with the D3DDECL_END() sentinel so it can be passed directly to
        // CreateVertexDeclaration() -- verify it, don't just assume the constant literal is right.
        check(elems != nullptr && elems[count].Stream == 0xFF && elems[count].Type == D3DDECLTYPE_UNUSED,
              "stride 52's array is properly D3DDECL_END()-terminated right after its 5 real elements");
    }
    {
        UINT count = 123;
        const D3DVERTEXELEMENT9* elems = VertexElementsForStrideD3D9(999, count);
        check(elems == nullptr && count == 0, "an unrecognized stride returns nullptr/count=0, not garbage");
    }

    std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
