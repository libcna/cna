// SPDX-License-Identifier: MS-PL
//
// FNA3D Effect Framework wrapper for CNA's rigid metallic-roughness shader.
// The shared shader is also the DirectX 9 renderer's compiler-verified PBR implementation; keeping
// the BRDF in one source prevents the two Shader Model 3 backends from drifting apart.
// Rebuild with:
//   fxc /nologo /Op /T fx_2_0 /Fo CnaPbrEffect.fxb CnaPbrEffect.fx

#define CNA_D3D9_IBL 1
#define CNA_D3D9_DOUBLE_SIDED 1
#include "../../directx9/src/shaders/cna/Pbr3D.hlsl"

technique CnaPbr
{
    pass Rigid
    {
        VertexShader = compile vs_3_0 VSPbr3D();
        PixelShader = compile ps_3_0 PSPbr3D();
    }
    pass RigidColor
    {
        VertexShader = compile vs_3_0 VSPbr3DColor();
        PixelShader = compile ps_3_0 PSPbr3D();
    }
}
