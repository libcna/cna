// SPDX-License-Identifier: MS-PL
#pragma once

// The two skinned vertex records the SkinnedEffect example programs write with SetDataRaw, as
// declarations. XNA has no undeclared vertex buffers, and GraphicsDevice refuses a skinned draw
// over one ("BlendIndices0 is missing"): the elements SkinnedEffect reads have to be named.

#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace CNA::Examples
{
    /**
     * @brief Position, Normal, TextureCoordinate0, BlendWeight0 and Byte4 BlendIndices0: 52 bytes.
     *
     * @return The declaration of the examples' 52-byte skinned record.
     */
    inline Microsoft::Xna::Framework::Graphics::VertexDeclaration SkinnedVertexDeclaration()
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        return VertexDeclaration(52, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
        });
    }

    /**
     * @brief The 52-byte skinned record followed by a Color0: 56 bytes.
     *
     * @return The declaration of the examples' 56-byte vertex-coloured skinned record.
     */
    inline Microsoft::Xna::Framework::Graphics::VertexDeclaration SkinnedColorVertexDeclaration()
    {
        using namespace Microsoft::Xna::Framework::Graphics;
        return VertexDeclaration(56, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
            VertexElement(52, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }
}
