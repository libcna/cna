// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-267: an effect the shader compiler refuses.
//
// The failure is deliberately an ordinary one -- a name that was never declared -- rather than a
// malformed file, so what is measured is the *compiler's* refusal reaching the build rather than
// the importer failing to read the source at all.
float4x4 WorldViewProjection;

float4 PixelMain() : COLOR0
{
    return NeverDeclared * 2;
}

technique Broken
{
    pass Single
    {
        PixelShader = compile ps_2_0 PixelMain();
    }
}
