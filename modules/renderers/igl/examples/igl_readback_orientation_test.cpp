// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-60 / IGL-67: the row order and the row ORIGIN of every readback this renderer
// performs, isolated from rendering entirely.
//
// `igl::IFramebuffer::copyBytesColorAttachment()` takes a `TextureRangeDesc` whose `y` its two
// backends measure from OPPOSITE edges -- OpenGL's `glReadPixels` counts from the bottom of the
// attachment, Vulkan's `vkCmdCopyImageToBuffer` counts from the top of the image -- and IGL does
// not reconcile them. It only reconciles the row ORDER of what comes back, by flipping the copied
// rectangle's rows on Vulkan (`flipImageVertical = true`, unconditionally) so that both backends
// hand back a rectangle stored bottom-first.
//
// The consequence is invisible for a full-attachment read, where y is 0 from either edge and the
// two conventions coincide, and wrong by an exact mirror for every sub-rectangle. That is why a
// full-surface diagnostic copy could show content the per-pixel reads of the same frame could not
// find (IGL-60's Bug C), and why uploading into a render target and reading it back returned the
// rows reversed (IGL-67).
//
// This test uses no draw calls at all: it uploads a vertically ASYMMETRIC pattern with SetData and
// reads it back, so the only thing under test is the transfer path. A test that rendered its
// pattern could not tell a readback flip apart from a projection flip -- the two cancel, which is
// exactly how this survived every rendering-based test the renderer already had.
//
// Registered on BOTH backends deliberately: a fix that made one backend right by making the other
// wrong would still pass on whichever one it was written against.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display, or no Vulkan WSI here).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    /// Row r carries red = r * 32, so every row is distinguishable and the pattern reads
    /// differently in each direction -- a mirrored readback cannot coincide with a correct one.
    /// Color has no default constructor, so every buffer here is filled with a value that no
    /// correct readback could produce.
    [[nodiscard]] Color Unwritten()
    {
        return Color(static_cast<bytecs>(1), static_cast<bytecs>(2), static_cast<bytecs>(3),
                     static_cast<bytecs>(4));
    }

    [[nodiscard]] Color RowColor(const int row)
    {
        return Color(static_cast<bytecs>(row * 32), static_cast<bytecs>(0),
                     static_cast<bytecs>(255), static_cast<bytecs>(255));
    }
}

class IglReadbackOrientationTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        RenderTarget2D target(device, kSize, kSize);

        std::vector<Color> uploaded(static_cast<std::size_t>(kSize) * kSize, Unwritten());
        for (int row = 0; row < kSize; ++row)
            for (int column = 0; column < kSize; ++column)
                uploaded[static_cast<std::size_t>(row) * kSize + column] = RowColor(row);
        target.SetData(uploaded.data(), static_cast<int>(uploaded.size()));

        std::vector<Color> full(static_cast<std::size_t>(kSize) * kSize, Unwritten());
        target.GetData(full.data(), static_cast<int>(full.size()));

        std::printf("uploaded rows : ");
        for (int row = 0; row < kSize; ++row)
            std::printf("%3d ", uploaded[static_cast<std::size_t>(row) * kSize].getRProperty());
        std::printf("\nfull readback : ");
        for (int row = 0; row < kSize; ++row)
            std::printf("%3d ", full[static_cast<std::size_t>(row) * kSize].getRProperty());
        std::printf("\n");

        bool fullMatches = true;
        for (int row = 0; row < kSize; ++row)
            if (full[static_cast<std::size_t>(row) * kSize].getRProperty() !=
                RowColor(row).getRProperty())
                fullMatches = false;
        ExpectTrue("a full-surface GetData returns the rows SetData uploaded, in order",
                   fullMatches);

        // The decisive probe. A one-row sub-rectangle at row r must be that same row -- this is
        // where the two backends' opposite y origins stop cancelling.
        std::printf("row-by-row sub-rect readback : ");
        bool everyRowMatches = true;
        for (int row = 0; row < kSize; ++row)
        {
            const Rectangle rect(0, row, kSize, 1);
            std::vector<Color> single(static_cast<std::size_t>(kSize), Unwritten());
            target.GetData(0, &rect, single.data(), 0, static_cast<int>(single.size()));
            std::printf("%3d ", single[0].getRProperty());
            if (single[0].getRProperty() != RowColor(row).getRProperty())
                everyRowMatches = false;
        }
        std::printf("\n");
        ExpectTrue("a one-row GetData at row r returns row r, not its mirror", everyRowMatches);

        // Which orientation does SAMPLING agree with? This is the question IGL-67 was blocked on,
        // and it cannot be answered by a readback alone: a readback flip and a storage flip cancel.
        // Drawing the uploaded target to the screen answers it, because what reaches the back
        // buffer went through the sampler, not through the transfer path.
        device.SetRenderTarget(nullptr);
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));

        SpriteBatch spriteBatch(device);
        spriteBatch.Begin();
        spriteBatch.Draw(target, Vector2(0.0f, 0.0f), Color::White);
        spriteBatch.End();

        std::vector<Color> sampled(static_cast<std::size_t>(kSize) * kSize, Unwritten());
        device.GetBackBufferData(sampled.data(), 0, static_cast<int>(sampled.size()));
        std::printf("sampled rows  : ");
        for (int row = 0; row < kSize; ++row)
            std::printf("%3d ", sampled[static_cast<std::size_t>(row) * kSize].getRProperty());
        std::printf("\n");

        bool sampledUpright = true;
        for (int row = 0; row < kSize; ++row)
            if (sampled[static_cast<std::size_t>(row) * kSize].getRProperty() !=
                RowColor(row).getRProperty())
                sampledUpright = false;
        ExpectTrue("drawing an uploaded render target puts row r of SetData at screen row r",
                   sampledUpright);

        // The same full-vs-sub-rectangle question for the BACK BUFFER, which reaches a different
        // CNA code path (IglRenderer::ReadBackbuffer) than a render target's own readback -- and
        // now against a genuinely non-uniform image, which a clear alone cannot provide.
        std::printf("back-buffer sub-rect readback : ");
        bool backBufferAgrees = true;
        for (int row = 0; row < kSize; ++row)
        {
            const Rectangle rect(0, row, 1, 1);
            Color single = Unwritten();
            device.GetBackBufferData(&rect, &single, 0, 1);
            std::printf("%3d ", single.getRProperty());
            if (single.getRProperty() !=
                sampled[static_cast<std::size_t>(row) * kSize].getRProperty())
                backBufferAgrees = false;
        }
        std::printf("\n");
        ExpectTrue("a 1x1 GetBackBufferData at row r agrees with the full-surface read of row r",
                   backBufferAgrees);

        // Does RENDERED content share a storage convention with UPLOADED content? It must: the
        // sampler, the readback and SetRenderTarget-then-draw all read one image, and a renderer
        // whose answer depends on how the pixels got there cannot be right for both. Rendering the
        // uploaded target into a second target answers it with the same asymmetric pattern.
        RenderTarget2D rendered(device, kSize, kSize);
        device.SetRenderTarget(&rendered);
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        spriteBatch.Begin();
        spriteBatch.Draw(target, Vector2(0.0f, 0.0f), Color::White);
        spriteBatch.End();
        device.SetRenderTarget(nullptr);

        std::vector<Color> renderedRows(static_cast<std::size_t>(kSize) * kSize, Unwritten());
        rendered.GetData(renderedRows.data(), static_cast<int>(renderedRows.size()));
        std::printf("rendered-into-target full readback : ");
        for (int row = 0; row < kSize; ++row)
            std::printf("%3d ", renderedRows[static_cast<std::size_t>(row) * kSize].getRProperty());
        std::printf("\n");

        bool renderedMatchesUploaded = true;
        for (int row = 0; row < kSize; ++row)
            if (renderedRows[static_cast<std::size_t>(row) * kSize].getRProperty() !=
                RowColor(row).getRProperty())
                renderedMatchesUploaded = false;
        ExpectTrue("a target rendered INTO reads back the same way as one uploaded INTO",
                   renderedMatchesUploaded);

        // The question the two readback answers alone cannot settle: a readback flip and a storage
        // flip are indistinguishable from a readback. Sampling reads the image with neither, so
        // drawing the RENDERED target to the screen says which orientation the sampler sees -- and
        // therefore which of the two storage conventions is the canonical one.
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        spriteBatch.Begin();
        spriteBatch.Draw(rendered, Vector2(0.0f, 0.0f), Color::White);
        spriteBatch.End();

        std::vector<Color> sampledRendered(static_cast<std::size_t>(kSize) * kSize, Unwritten());
        device.GetBackBufferData(sampledRendered.data(), 0,
                                 static_cast<int>(sampledRendered.size()));
        std::printf("sampled rendered target : ");
        for (int row = 0; row < kSize; ++row)
            std::printf("%3d ",
                        sampledRendered[static_cast<std::size_t>(row) * kSize].getRProperty());
        std::printf("\n");

        bool renderedSamplesUpright = true;
        for (int row = 0; row < kSize; ++row)
            if (sampledRendered[static_cast<std::size_t>(row) * kSize].getRProperty() !=
                RowColor(row).getRProperty())
                renderedSamplesUpright = false;
        ExpectTrue("drawing a target that was RENDERED into is upright, like an uploaded one",
                   renderedSamplesUpright);
    }

public:
    IglReadbackOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglReadbackOrientationTest>();
}
