// SPDX-License-Identifier: MS-PL
// DX-223: renderer-neutral behavioral contract for ShaderEffect reflection and 3D drawing.
// The layout and register gaps are deliberate: a fixed SpriteBatch ABI or fixed b0/t0/s0 binding
// cannot satisfy this fixture accidentally.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_RENDERER_DIRECTX11)
    constexpr const char* kRendererName = "D3D11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr const char* kRendererName = "D3D12";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EasyGL";
#else
#error "ShaderEffect reflection contract requires EasyGL, DirectX 11, or DirectX 12"
#endif

    constexpr int kSize = 32;
    constexpr float U(int value) { return static_cast<float>(value) / 255.0f; }

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
            VertexElement(16, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(36, VertexElementFormat::Single,
                          VertexElementUsage::TextureCoordinate, 1),
        });

#if defined(CNA_RENDERER_EASYGL)
    const char* kVertexShader = R"GLSL(#version 300 es
precision highp float;
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

    const char* kPixelShader = R"GLSL(#version 300 es
precision highp float;
in vec4 vertexColor;
in vec2 textureUv;
in float signal;
out vec4 FragColor;
uniform sampler2D textureTwo;
uniform samplerCube textureCubeFour;
precision highp sampler3D;
uniform sampler3D textureVolumeSix;
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
                        texture(textureVolumeSix, vec3(0.5)).b);
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
#else
    const char* kVertexShader = R"HLSL(
struct VSIn
{
    float3 position : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL0;
    float signal : TEXCOORD1;
};
struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float signal : TEXCOORD1;
};
cbuffer TransformBlock : register(b3)
{
    float4x4 World;
    float4x4 View;
    float4x4 Projection;
};
VSOut main(VSIn input)
{
    VSOut output;
    float4 position = mul(World, float4(input.position, 1.0));
    position = mul(View, position);
    output.position = mul(Projection, position);
    output.color = input.color;
    output.uv = input.uv;
    output.signal = input.signal * input.normal.z;
    return output;
}
)HLSL";

    const char* kPixelShader = R"HLSL(
Texture2D textureTwo : register(t2);
SamplerState samplerTwo : register(s2);
TextureCube textureCubeFour : register(t4);
SamplerState samplerCubeFour : register(s4);
Texture3D textureVolumeSix : register(t6);
SamplerState samplerVolumeSix : register(s6);
cbuffer MaterialBlock : register(b5)
{
    float scalarValue;
    float2 pairValue;
    float3 tripleValue;
    float4 tintValue;
    int modeValue;
    float weights[2];
    float2 offsets[2];
    float3 colors[2];
    float4x4 manualMatrix;
    float4x4 matrixArray[2];
};
struct PSIn
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float signal : TEXCOORD1;
};
float4 main(PSIn input) : SV_Target
{
    float3 sampled = float3(textureTwo.Sample(samplerTwo, input.uv).r,
                            textureCubeFour.Sample(
                                samplerCubeFour, float3(1.0, 0.0, 0.0)).g,
                            textureVolumeSix.Sample(
                                samplerVolumeSix, float3(0.5, 0.5, 0.5)).b);
    float3 reflected = float3(
        scalarValue + pairValue.x + tripleValue.x + tintValue.x + weights[0] +
            offsets[0].x + colors[0].x + manualMatrix[0][0] + matrixArray[0][0][0],
        pairValue.y + tripleValue.y + tintValue.y + weights[1] + offsets[1].y +
            colors[1].y + manualMatrix[1][1] + matrixArray[1][0][0],
        (modeValue == 7 ? 27.0 / 255.0 : 0.0) + tripleValue.z + tintValue.z +
            offsets[0].y + colors[0].z + colors[1].z + manualMatrix[2][2]);
    return float4((sampled + reflected) * input.color.rgb * input.signal, 1.0);
}
)HLSL";
#endif

    bool Near(const Color& actual, const Color& expected, int tolerance = 4)
    {
        const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(actual.getRProperty(), expected.getRProperty()) &&
               close(actual.getGProperty(), expected.getGProperty()) &&
               close(actual.getBProperty(), expected.getBProperty()) &&
               close(actual.getAProperty(), expected.getAProperty());
    }

    std::string Describe(const Color& color)
    {
        return "(" + std::to_string(color.getRProperty()) + "," +
               std::to_string(color.getGProperty()) + "," +
               std::to_string(color.getBProperty()) + "," +
               std::to_string(color.getAProperty()) + ")";
    }

    ContractVertex V(float x, float y)
    {
        return {x, y, 0.0f, 0xFFFFFFFFu, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 1.0f};
    }
}

class ShaderEffectReflectionContractTest final : public Game
{
    struct TextureSet
    {
        std::unique_ptr<Texture2D> texture2D;
        std::unique_ptr<TextureCube> textureCube;
        std::unique_ptr<Texture3D> texture3D;
    };

    std::unique_ptr<GraphicsDeviceManager> graphicsManager_;
    std::unique_ptr<ShaderEffect> effect_;
    TextureSet texturesA_;
    TextureSet texturesB_;
    bool done_ = false;
    int checks_ = 0;
    int passes_ = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        ++checks_;
        if (condition) ++passes_;
    }

    void CheckColor(const std::string& label, const Color& actual, const Color& expected)
    {
        Check(Near(actual, expected),
              label + ": got=" + Describe(actual) + " expected=" + Describe(expected));
    }

    static void FillCube(TextureCube& texture, const Color& color)
    {
        constexpr std::array<CubeMapFace, 6> faces{
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (const CubeMapFace face : faces)
            texture.SetData(face, &color, 1);
    }

    static TextureSet CreateTextureSet(
        GraphicsDevice& device, const Color& texture2DColor,
        const Color& cubeColor, const Color& volumeColor)
    {
        TextureSet result;
        result.texture2D = std::make_unique<Texture2D>(
            device, 1, 1, false, SurfaceFormat::Color);
        result.texture2D->SetData(&texture2DColor, 1);
        result.textureCube = std::make_unique<TextureCube>(
            device, 1, false, SurfaceFormat::Color);
        FillCube(*result.textureCube, cubeColor);
        result.texture3D = std::make_unique<Texture3D>(
            device, 1, 1, 1, false, SurfaceFormat::Color);
        result.texture3D->SetData(&volumeColor, 1);
        return result;
    }

    void SetParameters(bool useA)
    {
        const float zero2[2]{};
        const float zero4[4]{};
        const float zero6[6]{};
        const float zero32[32]{};
        float manual[16]{};

        if (useA)
        {
            effect_->SetUniformFloat("scalarValue", U(1));
            effect_->SetUniformVec2("pairValue", U(2), U(3));
            effect_->SetUniformVec3("tripleValue", U(4), U(5), U(6));
            effect_->SetUniformVec4("tintValue", U(7), U(8), U(9), 1.0f);
            effect_->SetUniformInt("modeValue", 7);
            const float weights[2]{U(10), U(11)};
            const float offsets[4]{U(12), U(13), U(14), U(15)};
            const float colors[6]{U(16), U(17), U(18), U(19), U(20), U(21)};
            effect_->SetUniformFloatArray("weights", weights, 2);
            effect_->SetUniformVec2Array("offsets", offsets, 2);
            effect_->SetUniformVec3Array("colors", colors, 2);
            manual[0] = U(22);
            manual[5] = U(23);
            manual[10] = U(24);
            effect_->SetUniformMat4("manualMatrix", manual);
            float matrices[32]{};
            matrices[0] = U(25);
            matrices[16] = U(26);
            effect_->SetUniformMat4Array("matrixArray", matrices, 2);
        }
        else
        {
            effect_->SetUniformFloat("scalarValue", 0.0f);
            effect_->SetUniformVec2("pairValue", 0.0f, 0.0f);
            effect_->SetUniformVec3("tripleValue", 0.0f, 0.0f, 0.0f);
            effect_->SetUniformVec4("tintValue", 0.0f, 0.0f, 0.0f, 1.0f);
            effect_->SetUniformInt("modeValue", 3);
            effect_->SetUniformFloatArray("weights", zero2, 2);
            effect_->SetUniformVec2Array("offsets", zero4, 2);
            effect_->SetUniformVec3Array("colors", zero6, 2);
            effect_->SetUniformMat4("manualMatrix", manual);
            effect_->SetUniformMat4Array("matrixArray", zero32, 2);
        }
    }

    void PrepareEffect(bool useA, const Matrix& world)
    {
        auto& device = getGraphicsDeviceProperty();
        effect_->setWorldProperty(world);
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());
        effect_->Apply();
        SetParameters(useA);

        TextureSet& textures = useA ? texturesA_ : texturesB_;
        effect_->SetTexture(2, *textures.texture2D);
        effect_->SetTexture(4, *textures.textureCube);
        effect_->SetTexture(6, *textures.texture3D);
        effect_->SetUniformInt("textureTwo", 2);
        effect_->SetUniformInt("textureCubeFour", 4);
        effect_->SetUniformInt("textureVolumeSix", 6);
        device.getSamplerStatesProperty()[2] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[4] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[6] = SamplerState::PointClamp;
    }

    void SetDrawState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
    }

    Color DrawQuad(bool useA, bool indexed, int samples,
                   float left = -1.0f, float right = 1.0f,
                   const Matrix& world = Matrix::getIdentityProperty())
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, samples,
                              RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        SetDrawState(device);
        device.Clear(Color(5, 7, 11, 255));
        PrepareEffect(useA, world);

        const std::array<ContractVertex, 4> quad{
            V(left, 1.0f), V(left, -1.0f), V(right, -1.0f), V(right, 1.0f),
        };
        if (indexed)
        {
            const std::uint16_t indices[6]{0, 1, 2, 0, 2, 3};
            device.DrawUserIndexedPrimitives(
                PrimitiveType::TriangleList, quad.data(), 0, 4,
                indices, 0, 2, kDeclaration);
        }
        else
        {
            const std::array<ContractVertex, 6> triangles{
                quad[0], quad[1], quad[2], quad[0], quad[2], quad[3],
            };
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, triangles.data(), 0, 2, kDeclaration);
        }

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Color centre(0, 0, 0, 0);
        const Rectangle pixel(kSize / 2, kSize / 2, 1, 1);
        target.GetData(0, &pixel, &centre, 0, 1);
        return centre;
    }

    int DrawLine()
    {
        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        SetDrawState(device);
        device.Clear(Color(5, 7, 11, 255));
        PrepareEffect(false, Matrix::getIdentityProperty());
        const ContractVertex line[2]{V(-1.0f, 0.0f), V(1.0f, 0.0f)};
        device.DrawUserPrimitives(PrimitiveType::LineList, line, 0, 1, kDeclaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(kSize * kSize);
        target.GetData(0, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        return static_cast<int>(std::count_if(
            pixels.begin(), pixels.end(),
            [](const Color& pixel) { return Near(pixel, Color(32, 48, 64, 255)); }));
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        effect_ = std::make_unique<ShaderEffect>(device, kVertexShader, kPixelShader);

        // B is uploaded last. The first A draw therefore cannot pass from upload-time residual
        // bindings; it must execute all three SetTexture calls at their nonzero slots.
        texturesA_ = CreateTextureSet(
            device, Color(64, 0, 0, 255), Color(0, 96, 0, 255), Color(0, 0, 128, 255));
        texturesB_ = CreateTextureSet(
            device, Color(32, 0, 0, 255), Color(0, 48, 0, 255), Color(0, 0, 64, 255));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        std::printf("ShaderEffect reflection contract on %s\n", kRendererName);

        Check(effect_ && effect_->IsEffectValid(),
              std::string("runtime shader compiles: ") +
                  (effect_ ? effect_->GetCompileErrorEXT() : "no effect"));
        if (!effect_ || !effect_->IsEffectValid())
        {
            Exit();
            return;
        }

        const Color expectedA(163, 207, 246, 255);
        const Color expectedB(32, 48, 64, 255);
        CheckColor("A indexed draw resolves named values, arrays and t2/t4/t6", 
                   DrawQuad(true, true, 0), expectedA);
        CheckColor("B non-indexed draw replaces every reflected value and texture",
                   DrawQuad(false, false, 0), expectedB);
        CheckColor("A after B proves reflected state does not leak through caches",
                   DrawQuad(true, true, 0), expectedA);

        CheckColor("World matrix moves a non-stock layout into the centre",
                   DrawQuad(true, true, 0, -0.8f, -0.4f,
                            Matrix::CreateTranslation(0.6f, 0.0f, 0.0f)),
                   expectedA);
        CheckColor("World matrix resets on the next draw",
                   DrawQuad(true, true, 0, -0.8f, -0.4f,
                            Matrix::getIdentityProperty()),
                   Color(5, 7, 11, 255));

        const int linePixels = DrawLine();
        Check(linePixels >= kSize / 2 && linePixels <= kSize * 3,
              "LineList selects a thin custom-shader topology (matching pixels=" +
                  std::to_string(linePixels) + ")");

        RenderTarget2D msaaProbe(getGraphicsDeviceProperty(), kSize, kSize, false,
                                 SurfaceFormat::Color, DepthFormat::None, 4,
                                 RenderTargetUsage::DiscardContents);
        const int appliedSamples = msaaProbe.getMultiSampleCountProperty();
        Check(appliedSamples > 1,
              "renderer applies multisampling to the custom-shader target (applied=" +
                  std::to_string(appliedSamples) + ")");
        if (appliedSamples > 1)
            CheckColor("custom PSO uses the bound target's real sample count",
                       DrawQuad(true, true, appliedSamples), expectedA);

        std::printf("SUMMARY: %d/%d checks passed\n", passes_, checks_);
        Exit();
    }

public:
    ShaderEffectReflectionContractTest()
    {
        graphicsManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsManager_->setPreferredBackBufferWidthProperty(64);
        graphicsManager_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return checks_ > 0 && passes_ == checks_ ? 0 : 1; }
};

int main()
{
    ShaderEffectReflectionContractTest game;
    game.Run();
    return game.Result();
}
