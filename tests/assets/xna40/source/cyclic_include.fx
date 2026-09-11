// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-267: a source that includes itself.
//
// The only cycle an effect build can actually contain: an `#include` graph that never terminates.
// A cyclic *content* reference needs two assets and an ExternalReference each way, which the
// single-item build this corpus drives cannot express.
#include "cyclic_include.fx"

float4 PixelMain() : COLOR0
{
    return float4(1, 1, 1, 1);
}

technique Cyclic
{
    pass Single
    {
        PixelShader = compile ps_2_0 PixelMain();
    }
}
