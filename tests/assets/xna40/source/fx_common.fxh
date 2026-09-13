// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-192: the header fx_include.fx includes.
//
// A header rather than a second effect, so the include is a real build dependency: change this
// file and the effect that includes it has to be rebuilt.
#ifndef CNA_FX_COMMON_INCLUDED
#define CNA_FX_COMMON_INCLUDED

float4x4 WorldViewProjection;

float4 TransformToClip(float4 position)
{
    return mul(position, WorldViewProjection);
}

#endif
