// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1809: a 2D SpriteBatch program gaining HDR bloom, in about ten lines.
//
// The claim this program exists to check is the entry cost. Everything below the "--- the ten
// lines" marker is an ordinary CNA 2D game: a texture, a SpriteBatch, one Draw. The engine layer
// is the pipeline object, two calls around that Draw, and four settings.
//
// Check A -- the renderer can run the passes, or the program SKIPs.
// Check B -- with the pipeline present but nothing enabled, the frame is UNCHANGED, pixel for
//            pixel, against the same scene drawn with no pipeline at all.
// Check C -- with bloom on, light spreads beyond the sprite's own edges.
// Check D -- the spread grows with intensity.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kFrame = 128;
    constexpr int kSpriteSize = 16;
}

class BloomExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> sprite_;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// The game's own drawing: one bright sprite in the middle of the screen.
    void DrawScene()
    {
        spriteBatch_->Begin();
        spriteBatch_->Draw(*sprite_,
                           Rectangle((kFrame - kSpriteSize) / 2, (kFrame - kSpriteSize) / 2,
                                     kSpriteSize, kSpriteSize),
                           Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

    std::vector<Color> ReadFrame(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (const System::NotSupportedException&)
        {
            std::printf("SKIP: this renderer has no readable back buffer\n");
            std::exit(77);
        }
        return pixels;
    }

    /// Light outside the sprite's own rectangle -- which is exactly what bloom adds and nothing
    /// else in this scene can produce.
    static int GlowOutsideTheSprite(const std::vector<Color>& pixels)
    {
        const int low = (kFrame - kSpriteSize) / 2;
        const int high = low + kSpriteSize;
        int glow = 0;
        for (int y = 0; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
            {
                if (x >= low && x < high && y >= low && y < high) continue;
                glow += pixels[static_cast<std::size_t>(y) * kFrame + x].getRProperty();
            }
        return glow;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        sprite_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        sprite_->SetData(&white, 1);

        // `CustomEffects` is necessary but not sufficient, and the difference is worth stating:
        // the Vulkan renderer reports it true and its ShaderEffect takes SPIR-V bytecode rather
        // than the GLSL source these passes hand it, so every pass compiles nothing and copies
        // through. Asking the pass itself is the only question with a reliable answer.
        CNA::Graphics::BloomPass probe(device);
        if (!device.SupportsCapability(GraphicsCapability::CustomEffects) ||
            !probe.isSupported(device))
        {
            std::printf("SKIP: this renderer compiles no post-process pass, so the chain copies "
                        "its input through (a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        // The same scene with no engine layer at all, for the comparison below.
        device.Clear(Color::Black);
        DrawScene();
        const std::vector<Color> withoutPipeline = ReadFrame(device);

        // --- the ten lines -------------------------------------------------------------------
        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setBloomThreshold(0.5f);
        settings.setBloomIntensity(1.5f);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        // -------------------------------------------------------------------------------------
        const std::vector<Color> bloomed = ReadFrame(device);

        // And the same pipeline with everything switched off, which must change nothing.
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const std::vector<Color> inert = ReadFrame(device);

        int differences = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (inert[i] != withoutPipeline[i]) ++differences;
        std::printf("    inert pipeline vs no pipeline: %d differing pixels of %zu\n", differences,
                    inert.size());
        check(differences == 0 && !pipeline.isUsingSceneTarget(),
              "a pipeline with nothing enabled renders the identical frame");

        const int plainGlow = GlowOutsideTheSprite(withoutPipeline);
        const int bloomGlow = GlowOutsideTheSprite(bloomed);
        std::printf("    light outside the sprite: %d without bloom, %d with\n", plainGlow,
                    bloomGlow);
        check(plainGlow == 0, "the scene itself puts no light outside the sprite");
        check(bloomGlow > 0, "bloom spread light beyond the sprite's own edges");

        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setBloomIntensity(4.0f);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const int strongerGlow = GlowOutsideTheSprite(ReadFrame(device));
        std::printf("    at intensity 4.0: %d\n", strongerGlow);
        check(strongerGlow > bloomGlow, "a higher bloom intensity spreads more light");

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    BloomExample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    try
    {
        (void)argc;
        (void)argv;
        BloomExample example;
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
