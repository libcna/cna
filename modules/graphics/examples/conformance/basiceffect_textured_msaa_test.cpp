// SPDX-License-Identifier: MS-PL
// A textured BasicEffect draw on a multisampled backbuffer renders TextureColor * DiffuseColor and
// really lands in the multisampled surface. Moved from the retired VULKAN suite (Task 904), where
// the textured stock pipeline once picked a single-sample render pass under MSAA.
//
// The backbuffer is requested through the ordinary XNA seam -- PresentationParameters.
// MultiSampleCount on a directly constructed GraphicsDevice -- and every expectation keys on the
// APPLIED count the device reports back. A device that clamps the request to zero has no
// multisample backbuffer to test and the program exits 77 saying so.
//
//   A  the device reports an applied multisample count above one;
//   B  a full-frame textured quad (VertexPositionTexture, 20-byte stride) reads back exactly
//      TextureColor * DiffuseColor = (200,100,50) * (0.8,0.4,0.6) = (160,40,30);
//   C  a textured triangle's diagonal edge resolves to intermediate coverage values, so the
//      textured draw was rasterized into the multisampled surface rather than a single-sample one;
//   D  a pixel well inside that triangle still carries the exact textured colour after the resolve.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = no multisample backbuffer on this device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kTolerance = 8;
    const Color kTexel(200, 100, 50, 255);
    const Vector3 kDiffuse(0.8f, 0.4f, 0.6f);
    const Color kExpected(160, 40, 30, 255);

    int g_result = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (!ok) g_result = 1;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    bool Near(const Color& got, const Color& want)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= kTolerance &&
               std::abs(got.getGProperty() - want.getGProperty()) <= kTolerance &&
               std::abs(got.getBProperty() - want.getBProperty()) <= kTolerance;
    }

    void PrepareFrame(GraphicsDevice& device)
    {
        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    }

    std::vector<Color> ReadAll(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color(0, 0, 0, 0));
        device.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }
}

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("basiceffect_textured_msaa"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
    parameters.setBackBufferWidthProperty(kSize);
    parameters.setBackBufferHeightProperty(kSize);
    parameters.setMultiSampleCountProperty(8);
    GraphicsDevice device(
        GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, parameters);

    const int applied = device.getPresentationParametersProperty().getMultiSampleCountProperty();
    if (applied <= 1)
    {
        std::printf("[SKIP] the device clamped MultiSampleCount 8 to %d: no multisample "
                    "backbuffer to draw into\n", applied);
        return CNA::Examples::kSkipExitCode;
    }
    Check(true, "A the backbuffer is multisampled, applied MultiSampleCount " +
                    std::to_string(applied));

    Texture2D texture(device, 1, 1);
    texture.SetData(&kTexel, 1);

    BasicEffect effect(device);
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&texture);
    effect.setDiffuseColorProperty(kDiffuse);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(Matrix::getIdentityProperty());
    effect.setProjectionProperty(Matrix::getIdentityProperty());

    // B: full-frame quad.
    {
        const Vector3 tl(-1.0f, 1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br(1.0f, -1.0f, 0.0f), tr(1.0f, 1.0f, 0.0f);
        const VertexPositionTexture quad[6] = {
            {tl, Vector2(0.0f, 0.0f)}, {bl, Vector2(0.0f, 1.0f)}, {br, Vector2(1.0f, 1.0f)},
            {tl, Vector2(0.0f, 0.0f)}, {br, Vector2(1.0f, 1.0f)}, {tr, Vector2(1.0f, 0.0f)},
        };
        PrepareFrame(device);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        const std::vector<Color> pixels = ReadAll(device);
        const Color centre = pixels[static_cast<std::size_t>((kSize / 2) * kSize + kSize / 2)];
        Check(Near(centre, kExpected),
              "B full-frame textured quad == TextureColor * DiffuseColor: got " + Show(centre) +
                  ", expected " + Show(kExpected));
    }

    // C/D: upper-left triangle whose hypotenuse runs corner to corner through pixel centres.
    {
        const VertexPositionTexture triangle[3] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)},
            {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)},
        };
        PrepareFrame(device);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
        const std::vector<Color> pixels = ReadAll(device);

        // The hypotenuse crosses one pixel per row; count the partially covered ones over a band.
        int intermediate = 0;
        for (int row = kSize / 2 - 8; row <= kSize / 2 + 8; ++row)
        {
            for (int x = kSize - row - 3; x <= kSize - row + 2; ++x)
            {
                const int red = pixels[static_cast<std::size_t>(row * kSize + x)].getRProperty();
                if (red > 10 && red < kExpected.getRProperty() - 10)
                    ++intermediate;
            }
        }
        Check(intermediate > 0,
              "C the textured triangle's diagonal resolves to intermediate coverage (" +
                  std::to_string(intermediate) + " edge pixels over 17 rows)");

        const Color inside = pixels[static_cast<std::size_t>(8 * kSize + 8)];
        Check(Near(inside, kExpected),
              "D a deep-interior pixel keeps TextureColor * DiffuseColor after the resolve: got " +
                  Show(inside));
        const Color outside = pixels[static_cast<std::size_t>((kSize - 8) * kSize + kSize - 8)];
        Check(outside.getRProperty() < 10 && outside.getGProperty() < 10 &&
                  outside.getBProperty() < 10,
              "D a pixel outside the triangle keeps the clear colour: got " + Show(outside));
    }

    return g_result;
}
