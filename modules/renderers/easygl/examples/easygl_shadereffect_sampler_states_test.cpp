// SPDX-License-Identifier: MS-PL
// living-room-simulator's R-33: "GraphicsDevice.SamplerStates[n] before a DrawIndexedPrimitives
// that draws with a ShaderEffect never reaches GL on EasyGL". XNA applies every sampler slot on
// every draw, whatever the effect, and a custom pass that asks for point sampling must get it.
//
// Each unit samples a two-texel texture at u = 0.5, exactly between the texels: point sampling
// returns one texel, linear sampling their average. Unit 0 holds (red | blue) and drives R and B;
// unit 1 holds (green | black) and drives G. The two cases swap which unit is Point and which is
// Linear, so a renderer that ignored the slots, or applied one slot's state to both, fails one.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
void main() { gl_Position = vec4(aPosition, 1.0); }
)";

    const char* kFragmentSource = R"(#version 300 es
precision highp float;
uniform sampler2D uUnit0;
uniform sampler2D uUnit1;
out vec4 FragColor;
void main() {
    vec4 a = texture(uUnit0, vec2(0.5, 0.5));
    vec4 b = texture(uUnit1, vec2(0.5, 0.5));
    FragColor = vec4(a.r, b.g, a.b, 1.0);
}
)";

    bool IsMid(int value) { return std::abs(value - 128) <= 8; }
    bool IsEnd(int value) { return value <= 8 || value >= 247; }
}

class ShaderEffectSamplerStatesTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        ShaderEffect effect(device, kVertexSource, kFragmentSource);
        Texture2D unit0 = Texture2D::CreateFromPixels(device, 2, 1, {255, 0, 0, 255, 0, 0, 255, 255});
        Texture2D unit1 = Texture2D::CreateFromPixels(device, 2, 1, {0, 255, 0, 255, 0, 0, 0, 255});

        const VertexPositionTexture vertices[4] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)},
            {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)},
            {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)},
        };
        VertexBuffer vertexBuffer(device, 4);
        vertexBuffer.SetData(vertices, 4);
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer indexBuffer(device, 6);
        indexBuffer.SetData(indices, 6);

        const auto& viewport = device.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);

        struct Case
        {
            const char* name;
            const SamplerState* unit0;
            const SamplerState* unit1;
            bool unit0Linear;
        };
        const Case cases[] = {
            {"unit 0 Point, unit 1 Linear", &SamplerState::PointClamp, &SamplerState::LinearClamp,
             false},
            {"unit 0 Linear, unit 1 Point", &SamplerState::LinearClamp, &SamplerState::PointClamp,
             true},
        };
        for (const Case& testCase : cases)
        {
            device.Clear(Color::Black);
            effect.Apply();
            effect.SetTexture(0, unit0);
            effect.SetTexture(1, unit1);
            effect.SetUniformInt("uUnit0", 0);
            effect.SetUniformInt("uUnit1", 1);
            device.getSamplerStatesProperty()(0, *testCase.unit0);
            device.getSamplerStatesProperty()(1, *testCase.unit1);
            device.SetVertexBuffer(&vertexBuffer);
            device.setIndicesProperty(&indexBuffer);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);

            Color pixel(0, 0, 0, 0);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            const int r = pixel.getRProperty();
            const int g = pixel.getGProperty();
            const int b = pixel.getBProperty();
            const bool unit0Ok = testCase.unit0Linear ? (IsMid(r) && IsMid(b))
                                                      : (IsEnd(r) && IsEnd(b) && r != b);
            const bool unit1Ok = testCase.unit0Linear ? IsEnd(g) : IsMid(g);
            const bool pass = unit0Ok && unit1Ok;
            std::printf("[%s] %s: (%d,%d,%d)\n", pass ? "PASS" : "FAIL", testCase.name, r, g, b);
            if (!pass) { MarkFailedEXT(); }
        }
        device.SetVertexBuffer(nullptr);
        device.setIndicesProperty(nullptr);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<ShaderEffectSamplerStatesTest>();
}
