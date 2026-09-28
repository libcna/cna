// SPDX-License-Identifier: MS-PL
#pragma once

#include <d3dcompiler.h>

namespace CNA::Internal::Renderers::D3DCommon
{
    /// D3DReflect interface IID for the selected compiler ABI, independent of SDK UUID helpers.
#if D3D_COMPILER_VERSION <= 42
    inline constexpr IID kShaderReflectionIid =
        {0x17f27486, 0xa342, 0x4d10, {0x88, 0x42, 0xab, 0x08, 0x74, 0xe7, 0xf6, 0x70}};
#elif D3D_COMPILER_VERSION == 43
    inline constexpr IID kShaderReflectionIid =
        {0x0a233719, 0x3960, 0x4578, {0x9d, 0x7c, 0x20, 0x3b, 0x8b, 0x1d, 0x9c, 0xc1}};
#else
    inline constexpr IID kShaderReflectionIid =
        {0x8d536ca1, 0x0cca, 0x4956, {0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84}};
#endif
}
