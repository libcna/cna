// SPDX-License-Identifier: MS-PL
//
// FNA3D Effect Framework wrapper for CNA's skinned metallic-roughness shader.
// Rebuild with:
//   fxc /nologo /Op /T fx_2_0 /Fo CnaSkinnedPbrEffect.fxb CnaSkinnedPbrEffect.fx

#include "../../directx9/src/shaders/cna/PbrSkinned3D.hlsl"

technique CnaSkinnedPbr
{
    pass Skinned
    {
        VertexShader = compile vs_3_0 VSPbrSkinned3D();
        PixelShader = compile ps_3_0 PSPbrSkinned3D();
    }
    pass SkinnedColor
    {
        VertexShader = compile vs_3_0 VSPbrSkinned3DColor();
        PixelShader = compile ps_3_0 PSPbrSkinned3D();
    }
}
