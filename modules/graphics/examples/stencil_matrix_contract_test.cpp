// SPDX-License-Identifier: MS-PL
// SOFTWARE-121: renderer-neutral XNA stencil comparison, operation, topology and alpha ordering.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    std::array<VertexPositionColor, 6> FrontQuad(const Color& color, float depth = 0.5f)
    {
        return {{
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3(-1.0f, -1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
        }};
    }

    std::array<VertexPositionTexture, 6> TexturedQuad(float depth = 0.5f)
    {
        return {{
            {Vector3(-1.0f,  1.0f, depth), Vector2(0.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, depth), Vector2(0.0f, 1.0f)},
            {Vector3( 1.0f, -1.0f, depth), Vector2(1.0f, 1.0f)},
            {Vector3(-1.0f,  1.0f, depth), Vector2(0.0f, 0.0f)},
            {Vector3( 1.0f, -1.0f, depth), Vector2(1.0f, 1.0f)},
            {Vector3( 1.0f,  1.0f, depth), Vector2(1.0f, 0.0f)},
        }};
    }

    struct CompareCase
    {
        CompareFunction function;
        const char* name;
        std::array<bool, 3> expected;
    };

    constexpr std::array<int, 3> kReferences{64, 128, 192};
    constexpr std::array<const char*, 3> kRelations{"below", "equal", "above"};
    constexpr std::array<CompareCase, 8> kCompareCases{{
        {CompareFunction::Always,       "Always",       {true,  true,  true}},
        {CompareFunction::Never,        "Never",        {false, false, false}},
        {CompareFunction::Less,         "Less",         {true,  false, false}},
        {CompareFunction::LessEqual,    "LessEqual",    {true,  true,  false}},
        {CompareFunction::Equal,        "Equal",        {false, true,  false}},
        {CompareFunction::NotEqual,     "NotEqual",     {true,  false, true}},
        {CompareFunction::GreaterEqual, "GreaterEqual", {false, true,  true}},
        {CompareFunction::Greater,      "Greater",      {false, false, true}},
    }};

    struct OperationCase
    {
        StencilOperation operation;
        const char* name;
        int initial;
        int reference;
        int expected;
    };

    constexpr std::array<OperationCase, 8> kOperationCases{{
        {StencilOperation::Keep,                "Keep",                0x12, 0x3C, 0x12},
        {StencilOperation::Zero,                "Zero",                0x12, 0x3C, 0x00},
        {StencilOperation::Replace,             "Replace",             0x12, 0x3C, 0x3C},
        {StencilOperation::Increment,           "Increment wraps",     0xFF, 0x3C, 0x00},
        {StencilOperation::Decrement,           "Decrement wraps",     0x00, 0x3C, 0xFF},
        {StencilOperation::IncrementSaturation, "Increment saturates", 0xFF, 0x3C, 0xFF},
        {StencilOperation::DecrementSaturation, "Decrement saturates", 0x00, 0x3C, 0x00},
        {StencilOperation::Invert,              "Invert",              0x0F, 0x3C, 0xF0},
    }};
}

class StencilMatrixContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ++total_;
        if (ok)
            ++passed_;
    }

    Color Center(GraphicsDevice& device)
    {
        const Rectangle center(kSize / 2, kSize / 2, 1, 1);
        Color result;
        device.GetBackBufferData(&center, &result, 0, 1);
        return result;
    }

    bool IsGreen(const Color& color) const
    {
        return color.getGProperty() > 200 && color.getRProperty() < 48 &&
               color.getBProperty() < 48;
    }

    void Clear(GraphicsDevice& device, int stencil, float depth = 1.0f)
    {
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kBackground, depth, stencil);
    }

    static DepthStencilState StencilState(CompareFunction function, int reference)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(function);
        state.setReferenceStencilProperty(reference);
        return state;
    }

    void DrawFront(GraphicsDevice& device, const Color& color, float depth = 0.5f)
    {
        const auto quad = FrontQuad(color, depth);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

    bool ProbeStencil(GraphicsDevice& device, BasicEffect& basic, int expected)
    {
        DepthStencilState read = StencilState(CompareFunction::Equal, expected);
        device.setDepthStencilStateProperty(read);
        basic.Apply();
        DrawFront(device, kGreen);
        return IsGreen(Center(device));
    }

    void CheckComparisons(GraphicsDevice& device, BasicEffect& basic)
    {
        for (const auto& test : kCompareCases)
        {
            for (std::size_t relation = 0; relation < kReferences.size(); ++relation)
            {
                Clear(device, 128);
                DepthStencilState state = StencilState(test.function, kReferences[relation]);
                device.setDepthStencilStateProperty(state);
                basic.Apply();
                DrawFront(device, kGreen);
                const bool drawn = IsGreen(Center(device));
                Check(drawn == test.expected[relation],
                      std::string("StencilFunction ") + test.name + " with reference " +
                          kRelations[relation] + " stored value " +
                          (test.expected[relation] ? "passes" : "rejects"));
            }
        }
    }

    void CheckOperations(GraphicsDevice& device, BasicEffect& basic)
    {
        for (const auto& test : kOperationCases)
        {
            Clear(device, test.initial);
            DepthStencilState state = StencilState(CompareFunction::Always, test.reference);
            state.setStencilPassProperty(test.operation);
            device.setDepthStencilStateProperty(state);
            basic.Apply();
            DrawFront(device, kBackground);
            Check(ProbeStencil(device, basic, test.expected),
                  std::string("StencilPass ") + test.name + " produces the exact 8-bit value");
        }
    }

    void CheckWritePaths(GraphicsDevice& device, BasicEffect& basic)
    {
        Clear(device, 0x11);
        DepthStencilState fail = StencilState(CompareFunction::Never, 0x45);
        fail.setStencilFailProperty(StencilOperation::Replace);
        device.setDepthStencilStateProperty(fail);
        basic.Apply();
        DrawFront(device, kBackground);
        Check(ProbeStencil(device, basic, 0x45),
              "StencilFail writes the configured operation result");

        Clear(device, 0x11, 0.2f);
        DepthStencilState depthFail = StencilState(CompareFunction::Always, 0x56);
        depthFail.setDepthBufferEnableProperty(true);
        depthFail.setDepthBufferWriteEnableProperty(false);
        depthFail.setDepthBufferFunctionProperty(CompareFunction::Less);
        depthFail.setStencilDepthBufferFailProperty(StencilOperation::Replace);
        device.setDepthStencilStateProperty(depthFail);
        basic.Apply();
        DrawFront(device, kBackground, 0.8f);
        Check(ProbeStencil(device, basic, 0x56),
              "StencilDepthBufferFail writes only after a passing stencil test and failed depth test");
    }

    void StampTopology(GraphicsDevice& device, PrimitiveType primitive, bool indexed32)
    {
        if (primitive == PrimitiveType::LineList || primitive == PrimitiveType::LineStrip)
        {
            const VertexPositionColor line[2] = {
                {Vector3(-1.0f, 0.0f, 0.5f), kBackground},
                {Vector3( 1.0f, 0.0f, 0.5f), kBackground},
            };
            device.DrawUserPrimitives(primitive, line, 0, 1);
            return;
        }

        const VertexPositionColor strip[4] = {
            {Vector3(-1.0f,  1.0f, 0.5f), kBackground},
            {Vector3(-1.0f, -1.0f, 0.5f), kBackground},
            {Vector3( 1.0f,  1.0f, 0.5f), kBackground},
            {Vector3( 1.0f, -1.0f, 0.5f), kBackground},
        };
        if (!indexed32)
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleStrip, strip, 0, 2);
            return;
        }
        const std::uint32_t indices[4] = {0, 1, 2, 3};
        device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleStrip, strip, 0, 4, indices, 0, 2);
    }

    void CheckTopologies(GraphicsDevice& device, BasicEffect& basic)
    {
        struct TopologyCase
        {
            PrimitiveType primitive;
            bool indexed32;
            const char* name;
        };
        const TopologyCase cases[] = {
            {PrimitiveType::TriangleStrip, false, "non-indexed TriangleStrip"},
            {PrimitiveType::TriangleStrip, true,  "32-bit indexed TriangleStrip"},
            {PrimitiveType::LineList,      false, "LineList"},
            {PrimitiveType::LineStrip,     false, "LineStrip"},
        };

        for (const auto& test : cases)
        {
            Clear(device, 0);
            DepthStencilState state = StencilState(CompareFunction::Always, 0x37);
            state.setStencilPassProperty(StencilOperation::Replace);
            device.setDepthStencilStateProperty(state);
            basic.Apply();
            StampTopology(device, test.primitive, test.indexed32);
            Check(ProbeStencil(device, basic, 0x37),
                  std::string(test.name) + " uses the same stencil fragment path");
        }
    }

    void CheckAlphaDiscard(GraphicsDevice& device, BasicEffect& basic)
    {
        Clear(device, 0x05);
        DepthStencilState state = StencilState(CompareFunction::Always, 0x09);
        state.setStencilPassProperty(StencilOperation::Replace);
        device.setDepthStencilStateProperty(state);

        AlphaTestEffect rejected(device);
        rejected.setAlphaFunctionProperty(CompareFunction::Never);
        rejected.Apply();
        const auto quad = TexturedQuad();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);

        Check(ProbeStencil(device, basic, 0x05),
              "AlphaTestEffect discard occurs before StencilPass can modify storage");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        RasterizerState noCull;
        noCull.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(noCull);

        BasicEffect basic(device);
        basic.VertexColorEnabled = true;

        CheckComparisons(device, basic);
        CheckOperations(device, basic);
        CheckWritePaths(device, basic);
        CheckTopologies(device, basic);
        CheckAlphaDiscard(device, basic);

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    StencilMatrixContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
        graphics_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int Result() const { return result_; }
};

int main()
{
    StencilMatrixContractTest game;
    game.Run();
    return game.Result();
}
