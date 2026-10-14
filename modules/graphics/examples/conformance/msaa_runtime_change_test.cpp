// SPDX-License-Identifier: MS-PL
// Changing PreferMultiSampling on a live device (GraphicsDeviceManager.ApplyChanges) keeps every
// resource created before the change working, through repeated toggles, and the backbuffer's
// edges agree with the multisample count the device reports. Moved from the retired VULKAN suite
// (VULKAN-095).
//
// Every expectation keys on the APPLIED PresentationParameters.MultiSampleCount, never on the
// request: a renderer that can only choose its sample count at construction keeps reporting 0, and
// is then held to single-sample edges rather than to a multisample picture it never promised.
//
//   A  before any change, a Texture2D and a RenderTarget2D created up front render correctly;
//   B  after PreferMultiSampling = true: the SAME pre-change texture and target still render, a
//      resource created after the change works, the backbuffer renders the texture correctly, a
//      multisampled RenderTarget2D renders correctly, and a diagonal edge on the backbuffer has
//      intermediate coverage exactly when the applied count is above one;
//   C  after PreferMultiSampling = false: all of the above again, now with hard edges;
//   D  two more round trips; the third rebuild is where anything the first two leaked shows up.
//
// When no toggle ever produced an applied count above one the rebuild under test never happened:
// the program reports that and exits 77 (unless a check already failed).
//
// Exit code 0 = PASS, 1 = FAIL, 77 = the live device never engaged multisampling.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
}

class MsaaRuntimeChangeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int result_ = 0;
    bool engaged_ = false;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (!ok) result_ = 1;
    }

    static std::unique_ptr<Texture2D> MakeTexture(GraphicsDevice& dev, const Color& c)
    {
        auto t = std::make_unique<Texture2D>(dev, 2, 2, false, SurfaceFormat::Color);
        std::array<Color, 4> texels{c, c, c, c};
        t->SetData(texels.data(), static_cast<int>(texels.size()));
        return t;
    }

    static void Blit(GraphicsDevice& dev, Texture2D& texture, const Rectangle& destination)
    {
        SamplerState point = SamplerState::PointClamp;
        SpriteBatch batch(dev);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        batch.Draw(texture, destination, Rectangle(0, 0, 2, 2), Color(255, 255, 255, 255));
        batch.End();
    }

    /// Draws @p texture into @p rt through SpriteBatch and reads the 2x2 result back.
    static std::vector<Color> DrawInto(GraphicsDevice& dev, RenderTarget2D& rt, Texture2D& texture)
    {
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(13, 17, 19, 255));
        Blit(dev, texture, Rectangle(0, 0, 2, 2));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pixels(4, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, 4);
        return pixels;
    }

    /// Draws @p texture over the whole backbuffer and reads a 2x2 block back.
    static std::vector<Color> DrawIntoBackbuffer(GraphicsDevice& dev, Texture2D& texture)
    {
        dev.Clear(Color(13, 17, 19, 255));
        Blit(dev, texture, Rectangle(0, 0, kSize, kSize));
        std::vector<Color> pixels(4, Color(0, 0, 0, 0));
        const Rectangle region(8, 8, 2, 2);
        dev.GetBackBufferData(&region, pixels.data(), 0, 4);
        return pixels;
    }

    static std::vector<Color> DrawIntoMsaaTarget(GraphicsDevice& dev, Texture2D& texture)
    {
        RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 4,
                          RenderTargetUsage::DiscardContents);
        return DrawInto(dev, rt, texture);
    }

    static bool AllAre(const std::vector<Color>& pixels, const Color& want)
    {
        for (const Color& c : pixels)
            if (c.getRProperty() != want.getRProperty() || c.getGProperty() != want.getGProperty() ||
                c.getBProperty() != want.getBProperty())
                return false;
        return true;
    }

    /// Opaque white upper-left triangle over black; counts partially covered pixels along its
    /// diagonal over 17 rows. Zero on a single-sample backbuffer, positive on a resolved one.
    static int DiagonalEdgeBlend(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect effect(dev);
        effect.setVertexColorEnabledProperty(true);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const VertexPositionColor triangle[3] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Color::White},
            {Vector3(1.0f, 1.0f, 0.0f), Color::White},
            {Vector3(-1.0f, -1.0f, 0.0f), Color::White},
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color(0, 0, 0, 0));
        dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
        int blended = 0;
        for (int row = kSize / 2 - 8; row <= kSize / 2 + 8; ++row)
        {
            for (int x = kSize - row - 3; x <= kSize - row + 2; ++x)
            {
                const int red = pixels[static_cast<std::size_t>(row * kSize + x)].getRProperty();
                if (red > 20 && red < 235)
                    ++blended;
            }
        }
        return blended;
    }

    void CheckEdgesMatchAppliedCount(GraphicsDevice& dev, const char* leg)
    {
        const int applied = dev.getPresentationParametersProperty().getMultiSampleCountProperty();
        const int blended = DiagonalEdgeBlend(dev);
        if (applied > 1)
        {
            engaged_ = true;
            Check(blended > 0, std::string(leg) + " applied MultiSampleCount " +
                                   std::to_string(applied) +
                                   ": the backbuffer edge resolves to intermediate coverage (" +
                                   std::to_string(blended) + " pixels)");
        }
        else
        {
            Check(blended == 0, std::string(leg) + " applied MultiSampleCount " +
                                    std::to_string(applied) +
                                    ": the backbuffer edge stays hard (" + std::to_string(blended) +
                                    " blended pixels)");
        }
    }

    int ApplyPreferMultiSampling(GraphicsDevice& dev, bool prefer)
    {
        gdm_->setPreferMultiSamplingProperty(prefer);
        gdm_->ApplyChanges();
        return dev.getPresentationParametersProperty().getMultiSampleCountProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        // Created BEFORE any sample-count change.
        auto texture = MakeTexture(dev, kRed);
        auto target = std::make_unique<RenderTarget2D>(dev, 2, 2, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0,
                                                       RenderTargetUsage::DiscardContents);

        Check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "A before any change, the pre-existing texture and target render correctly");
        const int startCount = dev.getPresentationParametersProperty().getMultiSampleCountProperty();
        CheckEdgesMatchAppliedCount(dev, "A");

        // B. Turn multisampling on, on a live device.
        const int onCount = ApplyPreferMultiSampling(dev, true);
        std::printf("[INFO] MultiSampleCount %d -> %d after PreferMultiSampling=true\n",
                    startCount, onCount);
        Check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "B1 after enabling MSAA, the SAME pre-change texture and target still render");
        auto afterOn = MakeTexture(dev, kGreen);
        Check(AllAre(DrawInto(dev, *target, *afterOn), kGreen),
              "B2 a resource created after the change works too");
        Check(AllAre(DrawIntoBackbuffer(dev, *texture), kRed),
              "B3 the backbuffer renders the pre-change texture correctly");
        Check(AllAre(DrawIntoMsaaTarget(dev, *texture), kRed),
              "B4 a multisampled RenderTarget2D renders it correctly too");
        CheckEdgesMatchAppliedCount(dev, "B5");

        // C. Turn it off again.
        const int offCount = ApplyPreferMultiSampling(dev, false);
        std::printf("[INFO] MultiSampleCount %d -> %d after PreferMultiSampling=false\n",
                    onCount, offCount);
        Check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "C1 after disabling MSAA again, the original texture and target still render");
        Check(AllAre(DrawInto(dev, *target, *afterOn), kGreen),
              "C2 and so does the one created between the two changes");
        Check(AllAre(DrawIntoBackbuffer(dev, *texture), kRed),
              "C3 the backbuffer renders correctly again");
        CheckEdgesMatchAppliedCount(dev, "C4");

        // D. Two more round trips.
        ApplyPreferMultiSampling(dev, true);
        ApplyPreferMultiSampling(dev, false);
        Check(AllAre(DrawInto(dev, *target, *texture), kRed),
              "D1 a second on/off round trip leaves rendering correct");
        ApplyPreferMultiSampling(dev, true);
        Check(AllAre(DrawIntoBackbuffer(dev, *texture), kRed),
              "D2 the backbuffer is correct after the third rebuild");
        CheckEdgesMatchAppliedCount(dev, "D3");
        ApplyPreferMultiSampling(dev, false);
        Check(AllAre(DrawInto(dev, *target, *afterOn), kGreen),
              "D4 the resources survive the final change back to single-sample");

        if (!engaged_)
            std::printf("[SKIP] no PreferMultiSampling change produced an applied "
                        "MultiSampleCount above one on this live device, so the multisample "
                        "rebuild this program exists for never happened\n");
        Exit();
    }

public:
    MsaaRuntimeChangeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferMultiSamplingProperty(false);
    }

    [[nodiscard]] int getResult() const
    {
        if (result_ != 0) return 1;
        return engaged_ ? 0 : CNA::Examples::kSkipExitCode;
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("msaa_runtime_change"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    MsaaRuntimeChangeTest game;
    game.Run();
    return game.getResult();
}
