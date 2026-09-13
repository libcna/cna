// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-050: focused source ShaderEffect contract validation.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;
    constexpr float U(const int value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    struct ContractVertex
    {
        float x, y, z;
        std::uint32_t color;
        float u, v;
        float nx, ny, nz;
        float signal;
    };
    static_assert(sizeof(ContractVertex) == 40);

    const VertexDeclaration kDeclaration(
        40,
        {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            VertexElement(
                16, VertexElementFormat::Vector2,
                VertexElementUsage::TextureCoordinate, 0),
            VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(
                36, VertexElementFormat::Single,
                VertexElementUsage::TextureCoordinate, 1),
        });

    const char* kVertexShader = R"GLSL(#version 330 core
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in float inSignal;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
out vec4 vertexColor;
out vec2 textureUv;
out float signal;
void main()
{
    gl_Position = Projection * View * World * vec4(inPosition, 1.0);
    vertexColor = inColor;
    textureUv = inUv;
    signal = inSignal * inNormal.z;
}
)GLSL";

    const char* kFragmentShader = R"GLSL(#version 330 core
in vec4 vertexColor;
in vec2 textureUv;
in float signal;
out vec4 FragColor;
uniform sampler2D textureTwo;
uniform samplerCube textureCubeFour;
uniform float scalarValue;
uniform vec2 pairValue;
uniform vec3 tripleValue;
uniform vec4 tintValue;
uniform int modeValue;
uniform float weights[2];
uniform vec2 offsets[2];
uniform vec3 colors[2];
uniform mat4 manualMatrix;
uniform mat4 matrixArray[2];
void main()
{
    vec3 sampled = vec3(texture(textureTwo, textureUv).r,
                        texture(textureCubeFour, vec3(1.0, 0.0, 0.0)).g,
                        0.0);
    vec3 reflected = vec3(
        scalarValue + pairValue.x + tripleValue.x + tintValue.x + weights[0] +
            offsets[0].x + colors[0].x + manualMatrix[0][0] + matrixArray[0][0][0],
        pairValue.y + tripleValue.y + tintValue.y + weights[1] + offsets[1].y +
            colors[1].y + manualMatrix[1][1] + matrixArray[1][0][0],
        (modeValue == 7 ? 27.0 / 255.0 : 0.0) + tripleValue.z + tintValue.z +
            offsets[0].y + colors[0].z + colors[1].z + manualMatrix[2][2]);
    FragColor = vec4((sampled + reflected) * vertexColor.rgb * signal, 1.0);
}
)GLSL";

    const char* kBrokenFragmentShader = R"GLSL(#version 330 core
out vec4 FragColor;
void main()
{
    FragColor = definitely_not_a_declared_value;
}
)GLSL";

    const char* kSpriteVertexShader = R"GLSL(#version 330 core
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
uniform mat4 projection;
out vec2 textureUv;
out vec4 vertexColor;
void main()
{
    gl_Position = projection * vec4(inPosition, 0.0, 1.0);
    textureUv = inUv;
    vertexColor = inColor;
}
)GLSL";

    const char* kSpriteFragmentShader = R"GLSL(#version 330 core
in vec2 textureUv;
in vec4 vertexColor;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec4 tint;
void main()
{
    FragColor = texture(texture1, textureUv) * vertexColor * tint;
}
)GLSL";

    [[nodiscard]] ContractVertex MakeVertex(const float x, const float y)
    {
        return {x, y, 0.0f, 0xFFFFFFFFu, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f};
    }

    void FillCube(TextureCube& texture, const Color& color)
    {
        constexpr std::array<CubeMapFace, 6> faces{
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
            texture.SetData(face, &color, 1);
    }
}

class RlglShaderEffectTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglShaderEffectTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(
            PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetContextRecoveryEnabled(true);
        SetDrawState(device);

        const auto dialect = device.GetShaderDialectEXT();
        Check(
            device.SupportsCapability(CNA::GraphicsCapability::CustomEffects) &&
                device.ExecutesShaderEffectSourceEXT() &&
                dialect == CNA::Internal::Renderers::ShaderDialectEXT::GlslDesktop &&
                device.SupportsShaderLanguageEXT(
                    CNA::ShaderLanguageEXT::GlslDesktop,
                    CNA::ShaderStageEXT::Vertex) &&
                device.SupportsShaderLanguageEXT(
                    CNA::ShaderLanguageEXT::GlslDesktop,
                    CNA::ShaderStageEXT::Fragment),
            "source ShaderEffect capability and desktop-GLSL dialect are truthful");
        Check(
            !device.SupportsShaderLanguageEXT(
                CNA::ShaderLanguageEXT::GlslDesktop,
                CNA::ShaderStageEXT::Compute) &&
                !device.SupportsShaderLanguageEXT(
                    CNA::ShaderLanguageEXT::GlslEs,
                    CNA::ShaderStageEXT::Vertex) &&
                !device.SupportsShaderLanguageEXT(
                    static_cast<CNA::ShaderLanguageEXT>(999),
                    CNA::ShaderStageEXT::Fragment) &&
                !device.SupportsShaderLanguageEXT(
                    CNA::ShaderLanguageEXT::GlslDesktop,
                    static_cast<CNA::ShaderStageEXT>(999)),
            "unsupported shader languages and stages are refused");

        ShaderEffect broken(device, kVertexShader, kBrokenFragmentShader);
        const auto diagnostics = broken.GetShaderDiagnosticsEXT();
        Check(
            !broken.IsEffectValid() && !broken.GetCompileErrorEXT().empty() &&
                !diagnostics.empty() &&
                diagnostics.front().getStage() == CNA::ShaderStageEXT::Fragment &&
                diagnostics.front().getLine() > 0 &&
                !diagnostics.front().getMessage().empty(),
            "fragment compiler failure produces structured stage/line diagnostics");

        ShaderEffect effect(device, kVertexShader, kFragmentShader);
        if (!ExpectTrue(
                "desktop GLSL source program compiles and links",
                effect.IsEffectValid()))
            return;

        Texture2D textureA(device, 1, 1);
        Texture2D textureB(device, 1, 1);
        const Color redA(64, 0, 0, 255);
        const Color redB(32, 0, 0, 255);
        textureA.SetData(&redA, 1);
        textureB.SetData(&redB, 1);
        TextureCube cubeA(device, 1, false, SurfaceFormat::Color);
        TextureCube cubeB(device, 1, false, SurfaceFormat::Color);
        FillCube(cubeA, Color(0, 96, 0, 255));
        FillCube(cubeB, Color(0, 48, 0, 255));

        SetParameters(effect, true);
        DrawSource(device, effect, textureA, cubeA, true);
        ExpectPixel(
            "indexed source draw resolves declarations, matrices, uniforms, arrays and textures",
            Center(), Color(163, 207, 118, 255), 3);

        SetParameters(effect, false);
        DrawSource(device, effect, textureB, cubeB, false);
        ExpectPixel(
            "non-indexed source draw replaces reflected values and bindings",
            Center(), Color(32, 48, 0, 255), 3);

        SetParameters(effect, true);
        DrawSource(device, effect, textureA, cubeA, true);
        ExpectPixel(
            "source effect state can switch back without stale cached values",
            Center(), Color(163, 207, 118, 255), 3);

        auto& renderer = device.GetRenderer();
        renderer.DebugSimulateContextLoss();
        renderer.DebugRestoreContext();
        Check(
            renderer.CanBeginDrawEXT() && effect.IsEffectValid(),
            "source program is rebuilt during native context recovery");
        SetDrawState(device);
        DrawSource(device, effect, textureA, cubeA, true, false);
        ExpectPixel(
            "recovered source program replays cached uniforms and restored texture bindings",
            Center(), Color(163, 207, 118, 255), 3);

        BasicEffect stock(device);
        const Matrix identity = Matrix::getIdentityProperty();
        stock.setWorldProperty(identity);
        stock.setViewProperty(identity);
        stock.setProjectionProperty(identity);
        stock.setTextureEnabledProperty(false);
        stock.setVertexColorEnabledProperty(false);
        stock.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        const std::array<VertexPositionColor, 3> triangle{
            VertexPositionColor(Vector3(-0.8f, -0.8f, 0.0f), Color::White),
            VertexPositionColor(Vector3(0.8f, -0.8f, 0.0f), Color::White),
            VertexPositionColor(Vector3(0.0f, 0.8f, 0.0f), Color::White),
        };
        device.Clear(Color::Black);
        stock.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, triangle.data(), 0, 1);
        ExpectPixel(
            "source-to-stock transition restores the stock program and vertex inputs",
            Center(), Color::Blue);

        ShaderEffect spriteEffect(
            device, kSpriteVertexShader, kSpriteFragmentShader);
        if (!ExpectTrue(
                "SpriteBatch desktop GLSL program compiles and links",
                spriteEffect.IsEffectValid()))
            return;
        Texture2D white(device, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        SpriteBatch spriteBatch(device);
        spriteEffect.SetUniformVec4("tint", 1.0f, 0.0f, 1.0f, 1.0f);
        spriteEffect.SetUniformInt("texture1", 0);
        device.Clear(Color::Green);
        spriteBatch.Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            &SamplerState::PointClamp, nullptr, nullptr, &spriteEffect);
        spriteBatch.Draw(
            white, Rectangle(kWidth / 4, kHeight / 4, kWidth / 2, kHeight / 2),
            Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch.End();
        ExpectPixel(
            "SpriteBatch executes the same source program and preserves its uniform state",
            Center(), Color::Magenta);
        ExpectPixel(
            "source SpriteBatch draw leaves the background untouched",
            Rectangle(1, 1, 1, 1), Color::Green);
    }

private:
    [[nodiscard]] static Rectangle Center()
    {
        return Rectangle(kWidth / 2, kHeight / 2, 1, 1);
    }

    static void SetDrawState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[2] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[4] = SamplerState::PointClamp;
    }

    static void SetParameters(ShaderEffect& effect, const bool populated)
    {
        float manual[16]{};
        float matrices[32]{};
        const float zeros2[2]{};
        const float zeros4[4]{};
        const float zeros6[6]{};
        if (populated)
        {
            effect.SetUniformFloat("scalarValue", U(1));
            effect.SetUniformVec2("pairValue", U(2), U(3));
            effect.SetUniformVec3("tripleValue", U(4), U(5), U(6));
            effect.SetUniformVec4("tintValue", U(7), U(8), U(9), 1.0f);
            effect.SetUniformInt("modeValue", 7);
            const float weights[2]{U(10), U(11)};
            const float offsets[4]{U(12), U(13), U(14), U(15)};
            const float colors[6]{U(16), U(17), U(18), U(19), U(20), U(21)};
            effect.SetUniformFloatArray("weights", weights, 2);
            effect.SetUniformVec2Array("offsets", offsets, 2);
            effect.SetUniformVec3Array("colors", colors, 2);
            manual[0] = U(22);
            manual[5] = U(23);
            manual[10] = U(24);
            matrices[0] = U(25);
            matrices[16] = U(26);
        }
        else
        {
            effect.SetUniformFloat("scalarValue", 0.0f);
            effect.SetUniformVec2("pairValue", 0.0f, 0.0f);
            effect.SetUniformVec3("tripleValue", 0.0f, 0.0f, 0.0f);
            effect.SetUniformVec4("tintValue", 0.0f, 0.0f, 0.0f, 1.0f);
            effect.SetUniformInt("modeValue", 3);
            effect.SetUniformFloatArray("weights", zeros2, 2);
            effect.SetUniformVec2Array("offsets", zeros4, 2);
            effect.SetUniformVec3Array("colors", zeros6, 2);
        }
        effect.SetUniformMat4("manualMatrix", manual);
        effect.SetUniformMat4Array("matrixArray", matrices, 2);
        effect.SetUniformInt("textureTwo", 2);
        effect.SetUniformInt("textureCubeFour", 4);
    }

    static void DrawSource(
        GraphicsDevice& device, ShaderEffect& effect,
        Texture2D& texture, TextureCube& cube,
        const bool indexed, const bool setMatrices = true)
    {
        const Matrix identity = Matrix::getIdentityProperty();
        if (setMatrices)
        {
            effect.setWorldProperty(Matrix::CreateTranslation(0.6f, 0.0f, 0.0f));
            effect.setViewProperty(identity);
            effect.setProjectionProperty(identity);
        }
        effect.Apply();
        effect.SetTexture(2, texture);
        effect.SetTexture(4, cube);
        device.Clear(Color(5, 7, 11, 255));

        const std::array<ContractVertex, 4> quad{
            MakeVertex(-0.8f, 1.0f), MakeVertex(-0.8f, -1.0f),
            MakeVertex(-0.4f, -1.0f), MakeVertex(-0.4f, 1.0f),
        };
        if (indexed)
        {
            const std::uint32_t indices[6]{0, 1, 2, 0, 2, 3};
            device.DrawUserIndexedPrimitives(
                PrimitiveType::TriangleList, quad.data(), 0,
                static_cast<int>(quad.size()), indices, 0, 2, kDeclaration);
        }
        else
        {
            const std::array<ContractVertex, 6> triangles{
                quad[0], quad[1], quad[2], quad[0], quad[2], quad[3],
            };
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, triangles.data(), 0, 2, kDeclaration);
        }
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    return CNA::Examples::RunPixelTest<RlglShaderEffectTest>();
}
