// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-33: sampler-state conformance.
//
// FNA3D-12 claimed "blend / depth-stencil / rasterizer / sampler state, write masks, scissor,
// viewport" as done, but `Fna3d_State` covers everything in that list EXCEPT the sampler: the only
// sampler coverage anywhere was PointClamp inside the 2D test. FNA3D takes a full
// `FNA3D_SamplerState` per slot through `FNA3D_VerifySampler`, so filter, per-axis addressing,
// anisotropy and the LOD controls are all real state this renderer sets and nothing measured.
//
// Every check below reads pixels from a deliberately tiny texture magnified over a large
// destination, because that is the only way filtering and addressing are observable at all.
//
// Check A -- Point vs Linear magnification are distinguishable: point sampling reproduces the
//   source texels exactly, linear blends between them.
// Check B -- TextureAddressMode::Clamp, Wrap and Mirror each place the right texel outside [0,1].
// Check C -- addressU and addressV are independent: wrapping one axis while clamping the other
//   must affect only that axis.
// Check D -- Anisotropic is accepted and still samples the right colour (the renderer reports
//   AnisotropicFiltering true, so it must at minimum not degrade into something else).
// Check E -- MaxAnisotropy, MaxMipLevel and MipMapLevelOfDetailBias are accepted by the device
//   rather than rejected, and a draw after setting them still renders.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// 2x1: red then blue. Two texels is the minimum that makes filtering and wrapping visible.
    std::unique_ptr<Texture2D> MakeTwoTexelTexture(GraphicsDevice& device)
    {
        auto texture = std::make_unique<Texture2D>(device, 2, 1);
        const Color pixels[2] = { Color(255, 0, 0, 255), Color(0, 0, 255, 255) };
        texture->SetData(pixels, 2);
        return texture;
    }

    SamplerState MakeSampler(TextureFilter filter, TextureAddressMode u, TextureAddressMode v)
    {
        SamplerState state;
        state.setFilterProperty(filter);
        state.setAddressUProperty(u);
        state.setAddressVProperty(v);
        return state;
    }
}

class Fna3dSamplerTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        texture_ = MakeTwoTexelTexture(device);

        FilterChecks(device);
        AddressModeChecks(device);
        IndependentAxisChecks(device);
        AnisotropyAndLodChecks(device);
    }

private:
    std::unique_ptr<Texture2D> texture_;

    /// Draws the 2x1 texture over a 128x128 destination with the given sampler and source rect.
    void DrawMagnified(GraphicsDevice& device, SamplerState& sampler, const Rectangle& source)
    {
        SpriteBatch batch(device);
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
        batch.Draw(*texture_, Rectangle(0, 0, 128, 128), source, Color::White);
        batch.End();
    }

    // ---- Check A --------------------------------------------------------------------------
    void FilterChecks(GraphicsDevice& device)
    {
        SamplerState point = MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                                         TextureAddressMode::Clamp);
        DrawMagnified(device, point, Rectangle(0, 0, 2, 1));
        ExpectPixel("Point magnification keeps texel 0 exactly red",
                    Rectangle(16, 64, 1, 1), Color(255, 0, 0, 255), 2);
        ExpectPixel("Point magnification keeps texel 1 exactly blue",
                    Rectangle(112, 64, 1, 1), Color(0, 0, 255, 255), 2);
        // The midpoint under point sampling is still one of the two source texels, never a blend.
        const Color mid = ReadPixel(64, 64);
        Check(IsOneOf(mid, Color(255, 0, 0, 255), Color(0, 0, 255, 255)),
              ("Point magnification never blends: midpoint is a source texel, got " +
               Describe(mid)).c_str());

        SamplerState linear = MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp,
                                          TextureAddressMode::Clamp);
        DrawMagnified(device, linear, Rectangle(0, 0, 2, 1));
        const Color blended = ReadPixel(64, 64);
        Check(blended.getRProperty() > 20 && blended.getRProperty() < 235 &&
                  blended.getBProperty() > 20 && blended.getBProperty() < 235,
              ("Linear magnification blends the two texels at the midpoint, got " +
               Describe(blended)).c_str());
        Check(!IsOneOf(blended, Color(255, 0, 0, 255), Color(0, 0, 255, 255)),
              "Linear magnification is genuinely different from Point");
    }

    // ---- Check B --------------------------------------------------------------------------
    void AddressModeChecks(GraphicsDevice& device)
    {
        // A source rectangle twice the texture's width forces the sampler outside [0,1]: the
        // right-hand half of the destination is addressed at u in [1,2).
        const Rectangle doubleWidth(0, 0, 4, 1);

        SamplerState clamp = MakeSampler(TextureFilter::Point, TextureAddressMode::Clamp,
                                         TextureAddressMode::Clamp);
        DrawMagnified(device, clamp, doubleWidth);
        ExpectPixel("Clamp repeats the last texel past u=1",
                    Rectangle(112, 64, 1, 1), Color(0, 0, 255, 255), 2);

        SamplerState wrap = MakeSampler(TextureFilter::Point, TextureAddressMode::Wrap,
                                        TextureAddressMode::Wrap);
        DrawMagnified(device, wrap, doubleWidth);
        // u in [1,2) wraps back to [0,1): the second copy starts red again.
        ExpectPixel("Wrap starts the texture over past u=1",
                    Rectangle(72, 64, 1, 1), Color(255, 0, 0, 255), 2);
        ExpectPixel("Wrap's second copy ends on the last texel",
                    Rectangle(120, 64, 1, 1), Color(0, 0, 255, 255), 2);

        SamplerState mirror = MakeSampler(TextureFilter::Point, TextureAddressMode::Mirror,
                                          TextureAddressMode::Mirror);
        DrawMagnified(device, mirror, doubleWidth);
        // u in [1,2) mirrors: the second copy runs blue-then-red, the reverse of Wrap.
        ExpectPixel("Mirror reverses the texture past u=1",
                    Rectangle(72, 64, 1, 1), Color(0, 0, 255, 255), 2);
        ExpectPixel("Mirror's reflected copy ends on the first texel",
                    Rectangle(120, 64, 1, 1), Color(255, 0, 0, 255), 2);
    }

    // ---- Check C --------------------------------------------------------------------------
    void IndependentAxisChecks(GraphicsDevice& device)
    {
        // Wrap on U, Clamp on V. Only the horizontal axis may repeat.
        SamplerState mixed = MakeSampler(TextureFilter::Point, TextureAddressMode::Wrap,
                                         TextureAddressMode::Clamp);
        DrawMagnified(device, mixed, Rectangle(0, 0, 4, 2));

        // Horizontally the texture repeats (red appears again in the right half) ...
        ExpectPixel("addressU=Wrap repeats horizontally",
                    Rectangle(72, 64, 1, 1), Color(255, 0, 0, 255), 2);
        // ... and vertically the single row is clamped, so top and bottom agree.
        const Color top = ReadPixel(16, 8);
        const Color bottom = ReadPixel(16, 120);
        Check(ColorNear(top, bottom),
              ("addressV=Clamp leaves the single texel row uniform down the destination, top " +
               Describe(top) + " bottom " + Describe(bottom)).c_str());
    }

    // ---- Checks D and E -------------------------------------------------------------------
    void AnisotropyAndLodChecks(GraphicsDevice& device)
    {
        SamplerState aniso = MakeSampler(TextureFilter::Anisotropic, TextureAddressMode::Clamp,
                                         TextureAddressMode::Clamp);
        aniso.setMaxAnisotropyProperty(4);
        DrawMagnified(device, aniso, Rectangle(0, 0, 2, 1));
        ExpectPixel("Anisotropic still samples texel 0 as red",
                    Rectangle(16, 64, 1, 1), Color(255, 0, 0, 255), 8);
        ExpectPixel("Anisotropic still samples texel 1 as blue",
                    Rectangle(112, 64, 1, 1), Color(0, 0, 255, 255), 8);

        // The LOD controls have no visible effect on a single-level texture, so what is checked is
        // that setting them is accepted and does not break the following draw -- claiming a
        // filtered result this texture cannot produce would be measuring nothing.
        SamplerState lod = MakeSampler(TextureFilter::Linear, TextureAddressMode::Clamp,
                                       TextureAddressMode::Clamp);
        lod.setMaxAnisotropyProperty(16);
        lod.setMaxMipLevelProperty(0);
        lod.setMipMapLevelOfDetailBiasProperty(0.5f);
        bool accepted = true;
        try
        {
            DrawMagnified(device, lod, Rectangle(0, 0, 2, 1));
        }
        catch (const std::exception&)
        {
            accepted = false;
        }
        Check(accepted, "MaxAnisotropy / MaxMipLevel / MipMapLevelOfDetailBias are accepted");
        if (accepted)
        {
            ExpectPixel("a draw after setting the LOD controls still renders",
                        Rectangle(16, 64, 1, 1), Color(255, 0, 0, 255), 12);
        }
    }

    // ---- helpers --------------------------------------------------------------------------
    Color ReadPixel(int x, int y)
    {
        std::vector<Color> pixel(1, Color(0, 0, 0, 0));
        const Rectangle region(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, pixel.data(), 0, 1);
        return pixel[0];
    }

    static bool ColorNear(const Color& a, const Color& b, int tolerance = 4)
    {
        const auto close = [tolerance](int p, int q) {
            const int delta = p - q;
            return (delta < 0 ? -delta : delta) <= tolerance;
        };
        return close(a.getRProperty(), b.getRProperty())
            && close(a.getGProperty(), b.getGProperty())
            && close(a.getBProperty(), b.getBProperty());
    }

    static bool IsOneOf(const Color& got, const Color& first, const Color& second)
    {
        return ColorNear(got, first, 4) || ColorNear(got, second, 4);
    }

    static std::string Describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dSamplerTest>();
}
