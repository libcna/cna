// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-042: exact classic render-target format validation.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <array>
#include <cmath>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    constexpr int kWindowSize = 32;
    constexpr int kTargetSize = 4;

    [[nodiscard]] bool Near(
        const float actual, const float expected, const float tolerance = 0.01f)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    [[nodiscard]] bool NearVector4(const Vector4& actual)
    {
        return Near(actual.X, 2.0f) && Near(actual.Y, -1.0f) &&
            Near(actual.Z, 0.5f) && Near(actual.W, 4.0f);
    }
}

class RlglRenderTargetFormatTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglRenderTargetFormatTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWindowSize);
        graphics_->setPreferredBackBufferHeightProperty(kWindowSize);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        constexpr std::array supported{
            SurfaceFormat::Color,
            SurfaceFormat::Rgba1010102,
            SurfaceFormat::Rg32,
            SurfaceFormat::Rgba64,
            SurfaceFormat::Single,
            SurfaceFormat::Vector2,
            SurfaceFormat::Vector4,
            SurfaceFormat::HalfSingle,
            SurfaceFormat::HalfVector2,
            SurfaceFormat::HalfVector4,
            SurfaceFormat::HdrBlendable};
        constexpr std::array unsupported{
            SurfaceFormat::Bgr565,
            SurfaceFormat::Bgra5551,
            SurfaceFormat::Bgra4444,
            SurfaceFormat::Dxt1,
            SurfaceFormat::Dxt3,
            SurfaceFormat::Dxt5,
            SurfaceFormat::NormalizedByte2,
            SurfaceFormat::NormalizedByte4,
            SurfaceFormat::Alpha8};

        bool classificationsExact = true;
        for (const SurfaceFormat format : supported)
            classificationsExact = classificationsExact &&
                device.SupportsSurfaceFormatAsRenderTargetEXT(format);
        for (const SurfaceFormat format : unsupported)
            classificationsExact = classificationsExact &&
                !device.SupportsSurfaceFormatAsRenderTargetEXT(format);
        Check(classificationsExact,
              "live FBO probes advertise the eleven FNA/XNA classic target formats only");

        bool compressedRejected = false;
        try
        {
            RenderTarget2D compressed(
                device, kTargetSize, kTargetSize, false, SurfaceFormat::Dxt1,
                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            (void)compressed;
        }
        catch (const std::exception&)
        {
            compressedRejected = true;
        }
        Check(compressedRejected,
              "compressed target construction is refused instead of substituted");

        const auto makeTarget = [&](const SurfaceFormat format, const bool mipMap = false)
        {
            return std::make_unique<RenderTarget2D>(
                device, kTargetSize, kTargetSize, mipMap, format,
                DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
        };
        const auto drawHdrConstant = [&](RenderTarget2D& target)
        {
            device.SetRenderTarget(&target);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect effect(device);
            effect.setDiffuseColorProperty(Vector3(0.5f, -0.25f, 0.125f));
            effect.setAlphaProperty(4.0f);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.Apply();
            const std::array<VertexPositionColor, 6> quad{
                VertexPositionColor(Vector3(-1.0f, 1.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(1.0f, 1.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(1.0f, 1.0f, 0.0f), Color::White),
                VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color::White)};
            device.DrawUserPrimitives(
                PrimitiveType::TriangleList, quad.data(), 0, 2);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };
        const Rectangle centre(2, 2, 1, 1);

        auto color = makeTarget(SurfaceFormat::Color);
        drawHdrConstant(*color);
        Color colorValue;
        color->GetData(0, &centre, &colorValue, 0, 1);
        Check(colorValue == Color(255, 0, 128, 255),
              "Color MSAA target clamps the stock shader result to exact RGBA8");

        auto rgba1010102 = makeTarget(SurfaceFormat::Rgba1010102);
        drawHdrConstant(*rgba1010102);
        Rgba1010102 rgba1010102Value;
        rgba1010102->GetData(0, &centre, &rgba1010102Value, 0, 1);
        Check(rgba1010102Value == Rgba1010102(1.0f, 0.0f, 0.5f, 1.0f),
              "Rgba1010102 MSAA target preserves XNA's least-significant-red layout");

        auto rg32 = makeTarget(SurfaceFormat::Rg32);
        drawHdrConstant(*rg32);
        Rg32 rg32Value;
        rg32->GetData(0, &centre, &rg32Value, 0, 1);
        Check(rg32Value == Rg32(1.0f, 0.0f),
              "Rg32 MSAA target preserves exact unsigned-normalized RG words");

        auto rgba64 = makeTarget(SurfaceFormat::Rgba64);
        drawHdrConstant(*rgba64);
        Rgba64 rgba64Value;
        rgba64->GetData(0, &centre, &rgba64Value, 0, 1);
        const Vector4 rgba64Vector = rgba64Value.ToVector4();
        Check(Near(rgba64Vector.X, 1.0f, 0.0001f) &&
                  Near(rgba64Vector.Y, 0.0f, 0.0001f) &&
                  Near(rgba64Vector.Z, 0.5f, 0.0001f) &&
                  Near(rgba64Vector.W, 1.0f, 0.0001f),
              "Rgba64 MSAA target preserves exact normalized 16-bit output");

        auto single = makeTarget(SurfaceFormat::Single);
        drawHdrConstant(*single);
        float singleValue = 0.0f;
        single->GetData(0, &centre, &singleValue, 0, 1);
        Check(Near(singleValue, 2.0f),
              "Single MSAA target preserves an unclamped value above one");

        auto vector2 = makeTarget(SurfaceFormat::Vector2);
        drawHdrConstant(*vector2);
        Vector2 vector2Value;
        vector2->GetData(0, &centre, &vector2Value, 0, 1);
        Check(Near(vector2Value.X, 2.0f) && Near(vector2Value.Y, -1.0f),
              "Vector2 MSAA target preserves signed unclamped RG values");

        auto vector4 = makeTarget(SurfaceFormat::Vector4);
        drawHdrConstant(*vector4);
        Vector4 vector4Value;
        vector4->GetData(0, &centre, &vector4Value, 0, 1);
        Check(NearVector4(vector4Value),
              "Vector4 MSAA target preserves signed HDR RGBA values");

        auto halfSingle = makeTarget(SurfaceFormat::HalfSingle);
        drawHdrConstant(*halfSingle);
        HalfSingle halfSingleValue;
        halfSingle->GetData(0, &centre, &halfSingleValue, 0, 1);
        Check(Near(halfSingleValue.ToSingle(), 2.0f),
              "HalfSingle MSAA target preserves an unclamped half value");

        auto halfVector2 = makeTarget(SurfaceFormat::HalfVector2);
        drawHdrConstant(*halfVector2);
        HalfVector2 halfVector2Value;
        halfVector2->GetData(0, &centre, &halfVector2Value, 0, 1);
        const Vector2 halfVector2Result = halfVector2Value.ToVector2();
        Check(Near(halfVector2Result.X, 2.0f) && Near(halfVector2Result.Y, -1.0f),
              "HalfVector2 MSAA target preserves signed half RG values");

        auto halfVector4 = makeTarget(SurfaceFormat::HalfVector4, true);
        drawHdrConstant(*halfVector4);
        HalfVector4 halfVector4Value;
        halfVector4->GetData(0, &centre, &halfVector4Value, 0, 1);
        HalfVector4 halfVector4Mip;
        halfVector4->GetData(2, nullptr, &halfVector4Mip, 0, 1);
        Check(NearVector4(halfVector4Value.ToVector4()) &&
                  NearVector4(halfVector4Mip.ToVector4()),
              "HalfVector4 MSAA target and generated final mip preserve signed HDR RGBA");

        auto hdrBlendable = makeTarget(SurfaceFormat::HdrBlendable);
        drawHdrConstant(*hdrBlendable);
        HalfVector4 hdrValue;
        hdrBlendable->GetData(0, &centre, &hdrValue, 0, 1);
        Check(NearVector4(hdrValue.ToVector4()),
              "HdrBlendable aliases exact HalfVector4 target storage");

        RenderTarget2D uploaded(
            device, 2, 2, false, SurfaceFormat::Vector4,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        const std::array<Vector4, 4> uploadValues{
            Vector4(1.0f, 2.0f, 3.0f, 4.0f),
            Vector4(5.0f, 6.0f, 7.0f, 8.0f),
            Vector4(-1.0f, -2.0f, -3.0f, -4.0f),
            Vector4(0.25f, 0.5f, 0.75f, 1.0f)};
        uploaded.SetData(uploadValues.data(), static_cast<int>(uploadValues.size()));
        std::array<Vector4, 4> uploadedReadback{};
        uploaded.GetData(uploadedReadback.data(), 0, static_cast<int>(uploadedReadback.size()));
        bool uploadExact = true;
        for (std::size_t index = 0; index < uploadValues.size(); ++index)
        {
            const Vector4& expected = uploadValues[index];
            const Vector4& actual = uploadedReadback[index];
            uploadExact = uploadExact && Near(actual.X, expected.X) &&
                Near(actual.Y, expected.Y) && Near(actual.Z, expected.Z) &&
                Near(actual.W, expected.W);
        }
        Check(uploadExact,
              "Vector4 target CPU upload/readback retains 16-byte texels and top-row order");

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        BasicEffect sampled(device);
        sampled.setTextureEnabledProperty(true);
        sampled.setTextureProperty(vector2.get());
        sampled.setWorldProperty(Matrix::getIdentityProperty());
        sampled.setViewProperty(Matrix::getIdentityProperty());
        sampled.setProjectionProperty(Matrix::getIdentityProperty());
        sampled.Apply();
        const std::array<VertexPositionColorTexture, 6> sampledQuad{
            VertexPositionColorTexture(
                Vector3(-1.0f, 1.0f, 0.0f), Color::White, Vector2(0.0f, 0.0f)),
            VertexPositionColorTexture(
                Vector3(1.0f, 1.0f, 0.0f), Color::White, Vector2(1.0f, 0.0f)),
            VertexPositionColorTexture(
                Vector3(-1.0f, -1.0f, 0.0f), Color::White, Vector2(0.0f, 1.0f)),
            VertexPositionColorTexture(
                Vector3(-1.0f, -1.0f, 0.0f), Color::White, Vector2(0.0f, 1.0f)),
            VertexPositionColorTexture(
                Vector3(1.0f, 1.0f, 0.0f), Color::White, Vector2(1.0f, 0.0f)),
            VertexPositionColorTexture(
                Vector3(1.0f, -1.0f, 0.0f), Color::White, Vector2(1.0f, 1.0f))};
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, sampledQuad.data(), 0, 2);
        ExpectPixel(
            "Vector2 target samples with XNA RG11 channel expansion",
            Rectangle(kWindowSize / 2, kWindowSize / 2, 1, 1),
            Color::Magenta);
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    return CNA::Examples::RunPixelTest<RlglRenderTargetFormatTest>();
}
