// SPDX-License-Identifier: MS-PL
#pragma once

// Test-only helper, also copied into the relocated SDK fixture. Public API includes only.
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::TestSupport
{
    inline std::array<Microsoft::Xna::Framework::Color, 2> CompilerProducedEffectPixels(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        const std::vector<std::uint8_t>& bytes)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        Effect effect(device, bytes);
        auto& parameters = effect.getParametersProperty();
        if (!parameters["Transform"] || !parameters["Tint"] || !parameters["Weights"])
            throw std::runtime_error("CnaConformanceEffect reflection lost a required parameter");
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Weights"]->SetValue(std::vector<float>{0.0f, 1.0f});
        auto* technique = effect.getTechniquesProperty()["SecondTechnique"];
        if (!technique) throw std::runtime_error("CnaConformanceEffect lost SecondTechnique");
        effect.setCurrentTechniqueProperty(technique);
        struct Vertex { float x, y, z, u, v; };
        const Vertex quad[] = {
            {-1, 1, .5f, 0, 0}, {-1, -1, .5f, 0, 1}, {1, -1, .5f, 1, 1},
            {-1, 1, .5f, 0, 0}, {1, -1, .5f, 1, 1}, {1, 1, .5f, 1, 0}};
        const VertexDeclaration declaration(static_cast<int>(sizeof(Vertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});
        const Vector4 tints[] = {Vector4(.25f, .5f, .75f, 1), Vector4(.75f, .25f, .5f, .5f)};
        std::array<Color, 2> result;
        RenderTarget2D target(device, 8, 8);
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            device.SetRenderTarget(&target);
            try
            {
                device.Clear(Color::Magenta);
                device.setBlendStateProperty(BlendState::Opaque);
                device.setDepthStencilStateProperty(DepthStencilState::None);
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                parameters["Tint"]->SetValue(tints[index]);
                technique->getPassesProperty()[0]->Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleList,
                    static_cast<const void*>(quad), 0, 2, declaration);
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
            catch (...)
            {
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                throw;
            }
            Color pixels[64];
            target.GetData(pixels, 64);
            result[index] = pixels[4 * 8 + 4];
        }
        return result;
    }
}
