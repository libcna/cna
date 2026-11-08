// SPDX-License-Identifier: MS-PL
// HND-SPRITE-PROJ-01: genuine Refresh SpriteBatch.SetRenderState IL_0177-0188
// sets MatrixTransform=T*P. SpriteEffectCode's stock vs_2_0 has four dp4 oPos
// instructions (x/y/z/w), then mov oT0.xy,v1: division and smooth UV belong to
// the GPU. FNA SpriteBatch.cs:1469-1490 and SpriteEffect.fx:22-33 agree.
#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "common/PixelTestGame.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
constexpr int kSize = 64;
using Pixels = std::array<Color, kSize * kSize>;

// Each of the 8x8 point-sampled texels identifies BOTH coordinates, including alpha.
Color Texel(int column, int row) { return Color(24 * (column + 1), 24 * (row + 1), 37, 255); }

class ProjectiveMatrixTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    bool done_ = false;
    int failures_ = 0;
    int checks_ = 0;

    void Check(const Pixels& pixels, int x, int y, Color expected,
               const char* label, SpriteSortMode mode)
    {
        ++checks_;
        const Color actual = pixels[static_cast<std::size_t>(y * kSize + x)];
        if (actual == expected) return;
        ++failures_;
        std::printf("[FAIL] %s mode=%d (%d,%d): RGBA=%08x expected=%08x\n", label,
                    static_cast<int>(mode), x, y,
                    static_cast<unsigned>(actual.getPackedValueProperty()),
                    static_cast<unsigned>(expected.getPackedValueProperty()));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        Texture2D texture(device, 8, 8);
        std::array<Color, 64> texels{};
        for (int row = 0; row < 8; ++row)
            for (int column = 0; column < 8; ++column)
                texels[static_cast<std::size_t>(row * 8 + column)] = Texel(column, row);
        texture.SetData(texels.data(), static_cast<int>(texels.size()));
        RenderTarget2D target(device, kSize, kSize);
        SpriteBatch batch(device);
        const Matrix identity = Matrix::getIdentityProperty();
        for (const auto mode : {SpriteSortMode::Deferred, SpriteSortMode::Immediate})
        {
            const auto render = [&](const Matrix& matrix, float depth = .5f)
            {
                device.SetRenderTarget(&target);
                device.Clear(Color::Black);
                batch.Begin(mode, &BlendState::Opaque, &SamplerState::PointClamp,
                            &DepthStencilState::None, &RasterizerState::CullNone, nullptr, matrix);
                batch.Draw(texture, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 8, 8),
                           Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, depth);
                batch.End();
                device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                Pixels pixels{};
                target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                return pixels;
            };

            const auto unchanged = render(identity);
            Matrix affine = Matrix::CreateScale(.5f, .5f, 1.0f);
            affine.M41 = 8; affine.M42 = 16;
            const auto translated = render(affine);
            // Hand-derived affine images: 8 pixels per texel, or 4 after half scale;
            // the scaled rectangle covers [8,40) x [16,48). Compare every RGBA byte.
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                {
                    Check(unchanged, x, y, Texel(x / 8, y / 8), "identity", mode);
                    const Color expected = x >= 8 && x < 40 && y >= 16 && y < 48
                        ? Texel((x - 8) / 4, (y - 16) / 4) : Color::Black;
                    Check(translated, x, y, expected, "affine", mode);
                }

            Matrix projective = identity;
            projective.M14 = projective.M24 = 1.0f / 64.0f;
            const auto warped = render(projective);
            // W=1+(x+y)/64 gives screen (s,t)=(x/W,y/W), hence
            // (u,v)=(s,t)/(64-s-t). Pixel centres are s=X+.5,t=Y+.5.
            // Projected corners: (0,0),(32,0),(0,32),(64/3,64/3).
            // At (16,8): (u,v)=(16.5,8.5)/39 => texel (3,1).
            // Predividing corners but discarding W gives affine (u,v)=(s,t)/32
            // on the first triangle => texel (4,2), so footprint-only fixes fail.
            Check(warped, 16, 8, Texel(3, 1), "projective UV first triangle", mode);
            Check(warped, 8, 16, Texel(1, 3), "projective transposed UV", mode);
            // On the second triangle: 17.5/29=35/58 => texel (4,4).
            // Affine interpolation there gives 41/64 => texel (5,5).
            Check(warped, 17, 17, Texel(4, 4), "projective UV second triangle", mode);
            Check(warped, 4, 4, Texel(0, 0), "projective interior control", mode);
            Check(warped, 22, 22, Color::Black, "projective diagonal outside", mode);
            Check(warped, 40, 8, Color::Black, "projective right outside", mode);

            projective.M34 = 2;
            const auto depthW = render(projective); // .5*M34 makes W=2+(x+y)/64.
            // Now (u,v)=2*(s,t)/(64-s-t). (12,6) gives (25,13)/45,
            // texel (4,2); the lower-right corner is (16,16).
            Check(depthW, 12, 6, Texel(4, 2), "layerDepth contributes to W", mode);
            Check(depthW, 6, 12, Texel(2, 4), "layerDepth W transposed UV", mode);
            Check(depthW, 18, 18, Color::Black, "layerDepth W outside", mode);
        }
        std::printf("[%s] projective SpriteBatch: %d RGBA checks, %d failures\n",
                    failures_ == 0 ? "PASS" : "FAIL", checks_, failures_);
        Exit();
    }

public:
    ProjectiveMatrixTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(kSize);
        manager_->setPreferredBackBufferHeightProperty(kSize);
    }
    int Result() const { return failures_ == 0 && checks_ > 0 ? 0 : 1; }
};
}

int main()
{
    const auto renderer = CNA::GraphicsRendererSelection::GetSelected();
    if (CNA::getGraphicsRendererCategory(renderer) == CNA::GraphicsRendererCategory::Diagnostic)
    {
        std::printf("[SKIP] spritebatch_projective_matrix: %s has no pixel readback\n",
                    std::string(CNA::getGraphicsRendererName(renderer)).c_str());
        return CNA::Examples::kSkipExitCode;
    }
    if (!CNA::Examples::ProbeGpuDisplayAvailable()) return CNA::Examples::kSkipExitCode;
    ProjectiveMatrixTest game;
    game.Run();
    return game.Result();
}
