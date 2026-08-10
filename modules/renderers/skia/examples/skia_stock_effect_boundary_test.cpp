// SPDX-License-Identifier: MS-PL
// SKIA-94: public promotion gate for the AlphaTestEffect and DualTextureEffect stock wrappers.
// SKIA-93 proved bounded fragment-like components, but these effects publicly draw transformed
// primitives. Until that route exists, every property combination must reject before pixels.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::GpuDrawParams;

namespace
{
    constexpr std::array<CompareFunction, 8> kCompareFunctions = {
        CompareFunction::Always,
        CompareFunction::Never,
        CompareFunction::Less,
        CompareFunction::LessEqual,
        CompareFunction::Equal,
        CompareFunction::NotEqual,
        CompareFunction::GreaterEqual,
        CompareFunction::Greater,
    };

    const std::array<VertexPositionTexture, 6> kQuad = {
        VertexPositionTexture{Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f)},
        VertexPositionTexture{Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f)},
        VertexPositionTexture{Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
        VertexPositionTexture{Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f)},
        VertexPositionTexture{Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
        VertexPositionTexture{Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f)},
    };

    [[nodiscard]] bool ExpectedAlphaPass(CompareFunction function, int alpha, int reference)
    {
        switch (function)
        {
            case CompareFunction::Always: return true;
            case CompareFunction::Never: return false;
            case CompareFunction::Less: return alpha < reference;
            case CompareFunction::LessEqual: return alpha <= reference;
            case CompareFunction::Equal: return alpha == reference;
            case CompareFunction::NotEqual: return alpha != reference;
            case CompareFunction::GreaterEqual: return alpha >= reference;
            case CompareFunction::Greater: return alpha > reference;
        }
        return true;
    }

    [[nodiscard]] bool ForwardedAlphaPass(const GpuDrawParams& params, int alpha)
    {
        const float sample = static_cast<float>(alpha) / 255.0f;
        const bool selected = params.alphaTest[1] > 0.0f
            ? std::abs(sample - params.alphaTest[0]) < params.alphaTest[1]
            : sample < params.alphaTest[0];
        return (selected ? params.alphaTest[2] : params.alphaTest[3]) >= 0.0f;
    }

    [[nodiscard]] bool Near(float actual, float expected)
    {
        return std::abs(actual - expected) <= 0.00001f;
    }
}

class SkiaStockEffectBoundaryTest final : public Game
{
public:
    SkiaStockEffectBoundaryTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const Color sentinel(17, 34, 51, 255);
        const Color white(255, 255, 255, 255);
        Texture2D primary(device, 1, 1);
        Texture2D secondary(device, 1, 1);
        primary.SetData(&white, 1);
        secondary.SetData(&white, 1);
        device.Clear(sentinel);

        Check(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)
                  && !device.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
              "stock-effect promotion cannot contradict false ThreeD/CustomEffects capabilities");

        AlphaTestEffect alpha(device);
        alpha.setTextureProperty(&primary);
        alpha.setDiffuseColorProperty(Vector3(0.75f, 0.5f, 0.25f));
        alpha.setAlphaProperty(0.5f);
        alpha.setFogEnabledProperty(true);
        alpha.setFogStartProperty(2.0f);
        alpha.setFogEndProperty(7.0f);
        alpha.setFogColorProperty(Vector3(0.1f, 0.2f, 0.3f));
        alpha.setVertexColorEnabledProperty(true);
        alpha.setWorldProperty(Matrix::CreateTranslation(1.0f, 2.0f, 3.0f));
        alpha.setViewProperty(Matrix::CreateTranslation(4.0f, 5.0f, 6.0f));
        alpha.setProjectionProperty(Matrix::CreateTranslation(7.0f, 8.0f, 9.0f));
        alpha.setReferenceAlphaProperty(128);

        bool alphaForwardingMatches = true;
        bool alphaDrawsReject = true;
        for (CompareFunction function : kCompareFunctions)
        {
            alpha.setAlphaFunctionProperty(function);
            GpuDrawParams params;
            alpha.FillGpuDrawParams(params);
            alphaForwardingMatches &= params.texture0 == &primary.GetRenderer()
                && params.textureEnabled && params.vertexColorEnabled && params.fogEnabled;
            for (int sampleAlpha : {127, 128, 129})
            {
                alphaForwardingMatches &= ForwardedAlphaPass(params, sampleAlpha)
                    == ExpectedAlphaPass(function, sampleAlpha, 128);
            }
            alphaDrawsReject &= DrawRefusesThreeD(device, alpha);
        }
        Check(alphaForwardingMatches,
              "all AlphaTestEffect properties forward, including 24 compare decisions");
        Check(alphaDrawsReject,
              "every AlphaTestEffect compare mode rejects at the missing public primitive route");

        DualTextureEffect dual(device);
        dual.setDiffuseColorProperty(Vector3(0.8f, 0.4f, 0.2f));
        dual.setAlphaProperty(0.5f);
        dual.setFogStartProperty(3.0f);
        dual.setFogEndProperty(9.0f);
        dual.setFogColorProperty(Vector3(0.3f, 0.2f, 0.1f));
        dual.setWorldProperty(Matrix::CreateTranslation(2.0f, 3.0f, 4.0f));
        dual.setViewProperty(Matrix::CreateTranslation(5.0f, 6.0f, 7.0f));
        dual.setProjectionProperty(Matrix::CreateTranslation(8.0f, 9.0f, 10.0f));

        dual.setTextureProperty(&primary);
        dual.setTexture2Property(&secondary);
        dual.setVertexColorEnabledProperty(true);
        dual.setFogEnabledProperty(true);
        GpuDrawParams dualParams;
        dual.FillGpuDrawParams(dualParams);
        Check(dualParams.dualTexture && dualParams.textureEnabled
                  && dualParams.texture0 == &primary.GetRenderer()
                  && dualParams.texture1 == &secondary.GetRenderer()
                  && dualParams.vertexColorEnabled && dualParams.fogEnabled
                  && Near(dualParams.diffuseColor[0], 0.4f)
                  && Near(dualParams.diffuseColor[1], 0.2f)
                  && Near(dualParams.diffuseColor[2], 0.1f)
                  && Near(dualParams.diffuseColor[3], 0.5f),
              "DualTextureEffect forwards both textures, premultiplied tint, fog and vertex color");

        bool dualDrawsReject = true;
        for (bool fog : {false, true})
        {
            dual.setFogEnabledProperty(fog);
            for (bool vertexColor : {false, true})
            {
                dual.setVertexColorEnabledProperty(vertexColor);
                for (int textureMask = 0; textureMask < 4; ++textureMask)
                {
                    dual.setTextureProperty((textureMask & 1) ? &primary : nullptr);
                    dual.setTexture2Property((textureMask & 2) ? &secondary : nullptr);
                    dualDrawsReject &= DrawRefusesThreeD(device, dual);
                }
            }
        }
        Check(dualDrawsReject,
              "all 16 dual-texture/fog/vertex-color availability combinations reject before draw");

        Color untouched = Color::Transparent;
        const Rectangle center(8, 4, 1, 1);
        device.GetBackBufferData(&center, &untouched, 0, 1);
        Check(untouched == sentinel,
              "rejected stock 3D draws leave the raster target pixel-exactly unchanged");

        dual.setTextureProperty(&primary);
        dual.setTexture2Property(&secondary);
        SpriteBatch batch(device);
        std::string alphaError;
        std::string dualError;
        const bool alphaBeginRejected = BeginRefusesStockEffect(batch, alpha, alphaError);
        const bool dualBeginRejected = BeginRefusesStockEffect(batch, dual, dualError);
        Check(alphaBeginRejected && dualBeginRejected
                  && alphaError.find("stock 3D Effects require") != std::string::npos
                  && dualError.find("stock 3D Effects require") != std::string::npos,
              "SpriteBatch stock-effect refusal identifies the unsupported 3D primitive route");

        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        batch.Draw(primary, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 1, 1),
                   Color(0, 255, 0, 255));
        batch.End();
        Color recovered = Color::Transparent;
        device.GetBackBufferData(&center, &recovered, 0, 1);
        Check(recovered == Color(0, 255, 0, 255),
              "SpriteBatch remains reusable after both stock-effect promotion refusals");

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    static bool DrawRefusesThreeD(GraphicsDevice& device, Effect& effect)
    {
        try
        {
            effect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad.data(), 0, 2);
        }
        catch (const std::exception& exception)
        {
            return std::string(exception.what()).find(
                "Skia (raster 2D) does not support 3D: GraphicsDevice::DrawUserPrimitives")
                != std::string::npos;
        }
        return false;
    }

    static bool BeginRefusesStockEffect(SpriteBatch& batch, Effect& effect, std::string& error)
    {
        try
        {
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                        nullptr, nullptr, nullptr, &effect);
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return true;
        }
        return false;
    }

    void Check(bool condition, const char* label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaStockEffectBoundaryTest game;
    game.Run();
    return game.Result();
}
