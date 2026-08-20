#pragma once

// plans/plan_dx9.md Phase D9-2 (D9-21): XNA Blend/BlendFunction/CompareFunction/CullMode/FillMode/
// TextureAddressMode/TextureFilter/StencilOperation -> D3D9 render-state equivalents.
//
// design decision 12/11: this is D3D9's own table, not a D3DCommon consumer -- D3D9 has no state
// objects at all (design decision 11: SetRenderState/SetSamplerState sequences instead), and its
// enum names/values (D3DBLEND, D3DCMPFUNC, ...) are numerically distinct from D3D11's own
// (verified directly against d3d9types.h on this machine, not assumed from D3D11's table).

#include <d3d9.h>

namespace CNA::Internal::Renderers::DirectX9
{
    /// Maps an XNA Blend ordinal to the corresponding D3DBLEND. Returns D3DBLEND_ONE for an
    /// unrecognized ordinal (mirrors D3DCommon::BlendToD3D11's own default).
    D3DBLEND BlendToD3D9(int blend);

    /// Maps an XNA BlendFunction ordinal to the corresponding D3DBLENDOP. Returns
    /// D3DBLENDOP_ADD for an unrecognized ordinal.
    D3DBLENDOP BlendFunctionToD3D9(int blendFunction);

    /// Maps an XNA CompareFunction ordinal to the corresponding D3DCMPFUNC. Returns
    /// D3DCMP_ALWAYS for an unrecognized ordinal.
    D3DCMPFUNC CompareFunctionToD3D9(int compareFunction);

    /// Maps an XNA CullMode ordinal to the corresponding D3DCULL. D3D9, like D3D11, defines
    /// clockwise as front-facing by default (no FrontCounterClockwise-equivalent render state to
    /// flip it) -- same convention D3DCommon::CullModeToD3D11 documents: CullClockwiseFace culls
    /// the front (clockwise) faces -> D3DCULL_CW, CullCounterClockwiseFace culls the back
    /// (counter-clockwise) faces -> D3DCULL_CCW.
    D3DCULL CullModeToD3D9(int cullMode);

    /// Maps an XNA FillMode ordinal to the corresponding D3DFILLMODE.
    D3DFILLMODE FillModeToD3D9(int fillMode);

    /// Maps an XNA TextureAddressMode ordinal to the corresponding D3DTEXTUREADDRESS.
    D3DTEXTUREADDRESS TextureAddressModeToD3D9(int addressMode);

    /// Maps an XNA StencilOperation ordinal to the corresponding D3DSTENCILOP. Same
    /// Increment/Decrement (wrapping, D3DSTENCILOP_INCR/DECR) vs IncrementSaturation/
    /// DecrementSaturation (clamping, D3DSTENCILOP_INCRSAT/DECRSAT) distinction
    /// D3DCommon::StencilOperationToD3D11 documents. Returns D3DSTENCILOP_KEEP for an
    /// unrecognized ordinal.
    D3DSTENCILOP StencilOperationToD3D9(int stencilOperation);

    /// D3D9 has no single composed "filter" enum like D3D11_FILTER -- min/mag/mip filtering are
    /// three independent D3DSAMP_MINFILTER/MAGFILTER/MIPFILTER render states, each a
    /// D3DTEXTUREFILTERTYPE. This struct decomposes XNA's single composed TextureFilter ordinal
    /// into the three values D3D9 actually needs (D9-63 consumes this directly).
    struct D3D9FilterTriple
    {
        D3DTEXTUREFILTERTYPE min;
        D3DTEXTUREFILTERTYPE mag;
        D3DTEXTUREFILTERTYPE mip;
    };

    /// Maps an XNA TextureFilter ordinal to its (min, mag, mip) D3DTEXTUREFILTERTYPE triple.
    /// Mechanical decomposition of XNA's own min/mag/mip-modeled enumerator names (same rationale
    /// D3DCommon::TextureFilterToD3D11 documents for its single composed value) -- e.g.
    /// MinLinearMagPointMipLinear decomposes to {LINEAR, POINT, LINEAR}. TextureFilter::Anisotropic
    /// decomposes to {ANISOTROPIC, ANISOTROPIC, LINEAR} -- D3DTEXF_ANISOTROPIC is not a legal
    /// D3DSAMP_MIPFILTER value in D3D9 (only NONE/POINT/LINEAR are), so LINEAR is used for the mip
    /// stage, matching D3D11_FILTER_ANISOTROPIC's own conceptual all-anisotropic encoding as
    /// closely as D3D9's stricter mip-filter type list allows. Returns an all-LINEAR triple for an
    /// unrecognized ordinal.
    D3D9FilterTriple TextureFilterToD3D9(int textureFilter);
}
