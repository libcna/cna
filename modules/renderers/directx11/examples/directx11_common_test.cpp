// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md Phase DIRECTX3: unit checks for D3DCommon's format/state/vertex-layout mapping tables
// (DX-11-fmt/DX-12-state/DX-16-vtx). No device/window/GPU needed -- these are pure functions.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DVertexFormatHelper.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <cstdio>
#include <cstring>

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::D3DCommon;

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
    // DX-11-fmt: format mapping.
    check(SurfaceFormatToDxgi(static_cast<int>(SurfaceFormat::Color)) == DXGI_FORMAT_R8G8B8A8_UNORM,
          "SurfaceFormat::Color -> DXGI_FORMAT_R8G8B8A8_UNORM");
    check(SurfaceFormatToDxgi(static_cast<int>(SurfaceFormat::Dxt5)) == DXGI_FORMAT_BC3_UNORM,
          "SurfaceFormat::Dxt5 -> DXGI_FORMAT_BC3_UNORM");
    check(DepthFormatToDxgi(static_cast<int>(DepthFormat::None)) == DXGI_FORMAT_UNKNOWN,
          "DepthFormat::None -> DXGI_FORMAT_UNKNOWN");
    check(DepthFormatToDxgi(static_cast<int>(DepthFormat::Depth16)) == DXGI_FORMAT_D16_UNORM,
          "DepthFormat::Depth16 -> DXGI_FORMAT_D16_UNORM");
    check(DepthFormatToDxgi(static_cast<int>(DepthFormat::Depth24Stencil8)) == DXGI_FORMAT_D24_UNORM_S8_UINT,
          "DepthFormat::Depth24Stencil8 -> DXGI_FORMAT_D24_UNORM_S8_UINT");
    check(DepthFormatToDxgi(static_cast<int>(DepthFormat::Depth24)) == DXGI_FORMAT_D24_UNORM_S8_UINT,
          "DepthFormat::Depth24 falls back to the same combined format (D3D11 has no depth-only 24-bit format)");

    // DX-12-state: state mapping.
    check(BlendToD3D11(static_cast<int>(Blend::One)) == D3D11_BLEND_ONE, "Blend::One -> D3D11_BLEND_ONE");
    check(BlendToD3D11(static_cast<int>(Blend::SourceAlpha)) == D3D11_BLEND_SRC_ALPHA,
          "Blend::SourceAlpha -> D3D11_BLEND_SRC_ALPHA");
    check(BlendToD3D11(static_cast<int>(Blend::InverseSourceAlpha)) == D3D11_BLEND_INV_SRC_ALPHA,
          "Blend::InverseSourceAlpha -> D3D11_BLEND_INV_SRC_ALPHA");
    check(BlendFunctionToD3D11(static_cast<int>(BlendFunction::ReverseSubtract)) == D3D11_BLEND_OP_REV_SUBTRACT,
          "BlendFunction::ReverseSubtract -> D3D11_BLEND_OP_REV_SUBTRACT");
    check(CompareFunctionToD3D11(static_cast<int>(CompareFunction::LessEqual)) == D3D11_COMPARISON_LESS_EQUAL,
          "CompareFunction::LessEqual -> D3D11_COMPARISON_LESS_EQUAL");
    check(CullModeToD3D11(static_cast<int>(CullMode::None)) == D3D11_CULL_NONE,
          "CullMode::None -> D3D11_CULL_NONE");
    check(CullModeToD3D11(static_cast<int>(CullMode::CullClockwiseFace)) == D3D11_CULL_FRONT,
          "CullMode::CullClockwiseFace -> D3D11_CULL_FRONT (FrontCounterClockwise=FALSE convention)");
    check(CullModeToD3D11(static_cast<int>(CullMode::CullCounterClockwiseFace)) == D3D11_CULL_BACK,
          "CullMode::CullCounterClockwiseFace -> D3D11_CULL_BACK");
    check(FillModeToD3D11(static_cast<int>(FillMode::WireFrame)) == D3D11_FILL_WIREFRAME,
          "FillMode::WireFrame -> D3D11_FILL_WIREFRAME");
    check(FillModeToD3D11(static_cast<int>(FillMode::Solid)) == D3D11_FILL_SOLID,
          "FillMode::Solid -> D3D11_FILL_SOLID");
    check(TextureAddressModeToD3D11(static_cast<int>(TextureAddressMode::Mirror)) == D3D11_TEXTURE_ADDRESS_MIRROR,
          "TextureAddressMode::Mirror -> D3D11_TEXTURE_ADDRESS_MIRROR");
    check(TextureFilterToD3D11(static_cast<int>(TextureFilter::Anisotropic)) == D3D11_FILTER_ANISOTROPIC,
          "TextureFilter::Anisotropic -> D3D11_FILTER_ANISOTROPIC");
    check(TextureFilterToD3D11(static_cast<int>(TextureFilter::Point)) == D3D11_FILTER_MIN_MAG_MIP_POINT,
          "TextureFilter::Point -> D3D11_FILTER_MIN_MAG_MIP_POINT");

    // DX-16-vtx: stride-keyed vertex layouts.
    {
        UINT count = 0;
        const D3D11_INPUT_ELEMENT_DESC* elems = InputElementsForStride(16, count);
        check(elems != nullptr && count == 2 && std::strcmp(elems[0].SemanticName, "POSITION") == 0 &&
              std::strcmp(elems[1].SemanticName, "COLOR") == 0 && elems[1].AlignedByteOffset == 12,
              "stride 16 -> POSITION0+COLOR0, matching VertexPositionColor's own layout");
    }
    {
        UINT count = 0;
        const D3D11_INPUT_ELEMENT_DESC* elems = InputElementsForStride(24, count);
        check(elems != nullptr && count == 3 && elems[2].AlignedByteOffset == 16 &&
              std::strcmp(elems[2].SemanticName, "TEXCOORD") == 0,
              "stride 24 -> POSITION0+COLOR0+TEXCOORD0, matching VertexPositionColorTexture's own layout");
    }
    {
        UINT count = 0;
        const D3D11_INPUT_ELEMENT_DESC* elems = InputElementsForStride(52, count);
        check(elems != nullptr && count == 5 && elems[4].AlignedByteOffset == 48 &&
              std::strcmp(elems[4].SemanticName, "BLENDINDICES") == 0 &&
              elems[4].Format == DXGI_FORMAT_R8G8B8A8_UINT,
              "stride 52 -> full skinned layout, matching VertexPositionNormalTextureSkinned's own layout");
    }
    {
        UINT count = 123;
        const D3D11_INPUT_ELEMENT_DESC* elems = InputElementsForStride(999, count);
        check(elems == nullptr && count == 0, "an unrecognized stride returns nullptr/count=0, not garbage");
    }

    std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
