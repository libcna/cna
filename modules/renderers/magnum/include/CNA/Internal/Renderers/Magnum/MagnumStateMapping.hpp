// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <Magnum/GL/Mesh.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Sampler.h>

#include <cstddef>

namespace CNA::Internal::Renderers::Magnum
{
    namespace Mg = ::Magnum;

    /**
     * @brief Per-slot sampler state, as XNA's `SamplerState` expresses it.
     *
     * Magnum's GL wrapper has no sampler-object type -- filtering and wrapping are properties of
     * `GL::Texture2D` itself -- so a slot's requested state is remembered here and written onto
     * whichever texture is bound to that slot at draw time, instead of into a standalone GL
     * sampler the way the EasyGL renderer does.
     */
    struct MagnumSamplerState
    {
        /** @brief Raw `TextureFilter` ordinal (Linear=0, Point=1, Anisotropic=2, ...). */
        int filter = 0;
        /** @brief Raw `TextureAddressMode` ordinal for U (Wrap=0, Clamp=1, Mirror=2). */
        int addressU = 1;
        /** @brief Raw `TextureAddressMode` ordinal for V. */
        int addressV = 1;
        /** @brief `SamplerState.MaxAnisotropy`; only consulted for the Anisotropic filter. */
        int maxAnisotropy = 4;
    };

    /**
     * @brief Maps an XNA `Blend` ordinal onto its Magnum blend factor.
     *
     * @param xnaBlend Raw `Blend` ordinal (One=0, Zero=1, SourceColor=2, ...).
     * @return The matching `GL::Renderer::BlendFunction`; `One` for an unrecognized ordinal.
     */
    [[nodiscard]] Mg::GL::Renderer::BlendFunction ToBlendFunction(int xnaBlend);

    /**
     * @brief Maps an XNA `BlendFunction` ordinal onto its Magnum blend equation.
     *
     * @param xnaBlendFunction Raw `BlendFunction` ordinal (Add=0, Subtract=1, ReverseSubtract=2,
     *                         Max=3, Min=4).
     * @return The matching `GL::Renderer::BlendEquation`; `Add` for an unrecognized ordinal.
     */
    [[nodiscard]] Mg::GL::Renderer::BlendEquation ToBlendEquation(int xnaBlendFunction);

    /**
     * @brief Maps an XNA `CompareFunction` ordinal onto its Magnum depth function.
     *
     * @param xnaCompare Raw `CompareFunction` ordinal (Always=0, Never=1, Less=2, ...).
     * @return The matching `GL::Renderer::DepthFunction`; `Always` for an unrecognized ordinal.
     */
    [[nodiscard]] Mg::GL::Renderer::DepthFunction ToDepthFunction(int xnaCompare);

    /**
     * @brief Maps an XNA `CompareFunction` ordinal onto its Magnum stencil function.
     *
     * @param xnaCompare Raw `CompareFunction` ordinal (Always=0, Never=1, Less=2, ...).
     * @return The matching `GL::Renderer::StencilFunction`; `Always` for an unrecognized ordinal.
     */
    [[nodiscard]] Mg::GL::Renderer::StencilFunction ToStencilFunction(int xnaCompare);

    /**
     * @brief Maps an XNA `StencilOperation` ordinal onto its Magnum stencil operation.
     *
     * @param xnaOperation Raw `StencilOperation` ordinal (Keep=0, Zero=1, Replace=2, Increment=3,
     *                     Decrement=4, IncrementSaturation=5, DecrementSaturation=6, Invert=7).
     * @return The matching `GL::Renderer::StencilOperation`; `Keep` for an unrecognized ordinal.
     */
    [[nodiscard]] Mg::GL::Renderer::StencilOperation ToStencilOperation(int xnaOperation);

    /**
     * @brief Maps an XNA `PrimitiveType` onto its Magnum mesh primitive.
     *
     * @param primitive The primitive type of the draw being dispatched.
     * @return The matching `GL::MeshPrimitive`.
     * @throw System::InvalidOperationException if @p primitive is not a recognized XNA primitive.
     */
    [[nodiscard]] Mg::GL::MeshPrimitive ToMeshPrimitive(PrimitiveType primitive);

    /**
     * @brief Number of vertices a primitive count of the given type consumes.
     *
     * @param primitive      The primitive type of the draw being dispatched.
     * @param primitiveCount Number of primitives requested by the caller.
     * @return The vertex count the native draw must be issued with.
     * @throw System::InvalidOperationException if @p primitive is not a recognized XNA primitive.
     */
    [[nodiscard]] int VertexCountForPrimitives(PrimitiveType primitive, int primitiveCount);

    /**
     * @brief Decomposes an XNA `TextureFilter` ordinal into its minification, mipmap and
     *        magnification components.
     *
     * Every ordinal names all three, including `Linear` (linear/linear/linear) and `Point`
     * (nearest/nearest/nearest) -- dropping the mipmap term would make a texture that owns a real
     * mip chain never mip-filter under the default filter every game gets unless it says otherwise.
     *
     * @param xnaFilter        Raw `TextureFilter` ordinal.
     * @param minificationOut  Receives the minification filter.
     * @param mipmapOut        Receives the mipmap filter.
     * @param magnificationOut Receives the magnification filter.
     */
    void DecomposeTextureFilter(int xnaFilter,
                                Mg::GL::SamplerFilter& minificationOut,
                                Mg::GL::SamplerMipmap& mipmapOut,
                                Mg::GL::SamplerFilter& magnificationOut);

    /**
     * @brief Maps an XNA `TextureAddressMode` ordinal onto its Magnum sampler wrapping.
     *
     * @param xnaAddressMode Raw `TextureAddressMode` ordinal (Wrap=0, Clamp=1, Mirror=2).
     * @return The matching `GL::SamplerWrapping`; `ClampToEdge` for an unrecognized ordinal.
     */
    [[nodiscard]] Mg::GL::SamplerWrapping ToSamplerWrapping(int xnaAddressMode);
}
