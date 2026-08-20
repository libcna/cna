// SPDX-License-Identifier: MS-PL
// plans/plan_headless.md HEADLESS-60: confirm each HEADLESS-20/22/23/24 validation rule actually throws
// under HeadlessValidation and does *not* throw under HeadlessFast, in one dedicated place --
// proving the mode dial genuinely gates every rule, not just the one representative case
// (HEADLESS-21's draw-call index-count check, already covered in Headless_Smoke).
//
// Check A/B -- HEADLESS-20: HeadlessVertexBufferRenderer::SetData() with vertex_count exceeding
//   the buffer's declared capacity -- throws under Validation, does not throw under Fast. Talks
//   directly to the renderer object (via HeadlessRenderer::SharedState()) rather than
//   through the XNA VertexBuffer wrapper, since HEADLESS-20 is specifically about the renderer's
//   own Require() check, not any XNA-layer bounds check that might exist above it.
// Check C/D -- HEADLESS-20: HeadlessIndexBufferRenderer::SetData16() with index_count exceeding
//   capacity -- same shape, for the index-buffer side of the same rule.
// Check E/F -- HEADLESS-22: a DualTextureEffect draw with no Texture2 assigned -- throws under
//   Validation, does not throw under Fast (the Validation half was already shown once in
//   Headless_Effects; grouped here with its Fast-mode counterpart for a complete, self-contained
//   mode-dial proof of this specific rule).
// Check G/H -- HEADLESS-23: SetScissorRect() with a negative origin -- throws under Validation,
//   does not throw under Fast (Headless_ValidationExtras only showed the Validation half).
// Check I/J -- HEADLESS-24: ApplySamplerState() with an out-of-range slot (16, valid range is
//   0..15) -- throws under Validation, does not throw under Fast.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Headless;

class HeadlessModeDialTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<HeadlessRenderer&>(dev.GetRenderer());
        dev.Clear(Color::Black);

        // Checks A/B/C/D: HEADLESS-20, talking directly to the renderer objects so this is
        // specifically a test of the renderer's own Require() check.
        {
            HeadlessVertexBufferRenderer vbRenderer(renderer.SharedState(), /*vertexCapacity=*/3);
            const std::vector<std::uint8_t> junkVerts(5 * 16, 0);

            renderer.SetMode(HeadlessMode::Validation);
            bool vbValidationThrew = false;
            try { vbRenderer.SetData(junkVerts.data(), 5, 16); }
            catch (const HeadlessValidationException&) { vbValidationThrew = true; }
            check(vbValidationThrew,
                  "HEADLESS-20: VertexBuffer SetData() past capacity throws under HeadlessValidation");

            renderer.SetMode(HeadlessMode::Fast);
            bool vbFastThrew = false;
            try { vbRenderer.SetData(junkVerts.data(), 5, 16); }
            catch (const HeadlessValidationException&) { vbFastThrew = true; }
            check(!vbFastThrew,
                  "HEADLESS-20: VertexBuffer SetData() past capacity does not throw under HeadlessFast");

            HeadlessIndexBufferRenderer ibRenderer(renderer.SharedState(), /*indexCapacity=*/3, false);
            const std::uint16_t junkIndices[5] = {0, 1, 2, 0, 1};

            renderer.SetMode(HeadlessMode::Validation);
            bool ibValidationThrew = false;
            try { ibRenderer.SetData16(junkIndices, 5); }
            catch (const HeadlessValidationException&) { ibValidationThrew = true; }
            check(ibValidationThrew,
                  "HEADLESS-20: IndexBuffer SetData16() past capacity throws under HeadlessValidation");

            renderer.SetMode(HeadlessMode::Fast);
            bool ibFastThrew = false;
            try { ibRenderer.SetData16(junkIndices, 5); }
            catch (const HeadlessValidationException&) { ibFastThrew = true; }
            check(!ibFastThrew,
                  "HEADLESS-20: IndexBuffer SetData16() past capacity does not throw under HeadlessFast");

            renderer.SetMode(HeadlessMode::Validation);
        }

        // Checks E/F: HEADLESS-22, via a DualTextureEffect draw missing its second texture.
        {
            Texture2D tex = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
            const VertexPositionTexture verts[3] = {
                { Vector3(0.0f, 1.0f, 0.0f), Vector2(0.5f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };

            auto drawWithMissingTexture2 = [&]() {
                VertexBuffer vb(dev, VertexPositionTexture::getVertexDeclarationStatic(), 3, BufferUsage::None);
                vb.SetData(verts, 3);
                DualTextureEffect fx(dev);
                fx.setTextureProperty(&tex);
                // setTexture2Property() deliberately not called.
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                dev.SetVertexBuffer(nullptr);
            };

            renderer.SetMode(HeadlessMode::Validation);
            bool dualValidationThrew = false;
            try { drawWithMissingTexture2(); }
            catch (const HeadlessValidationException&) { dualValidationThrew = true; }
            check(dualValidationThrew,
                  "HEADLESS-22: DualTextureEffect draw missing Texture2 throws under HeadlessValidation");

            renderer.SetMode(HeadlessMode::Fast);
            bool dualFastThrew = false;
            try { drawWithMissingTexture2(); }
            catch (const HeadlessValidationException&) { dualFastThrew = true; }
            check(!dualFastThrew,
                  "HEADLESS-22: DualTextureEffect draw missing Texture2 does not throw under HeadlessFast");

            renderer.SetMode(HeadlessMode::Validation);
        }

        // Checks G/H: HEADLESS-23, SetScissorRect() with a negative origin.
        {
            renderer.SetMode(HeadlessMode::Validation);
            bool scissorValidationThrew = false;
            try { renderer.SetScissorRect(-1, -1, 4, 4); }
            catch (const HeadlessValidationException&) { scissorValidationThrew = true; }
            check(scissorValidationThrew,
                  "HEADLESS-23: SetScissorRect() with a negative origin throws under HeadlessValidation");

            renderer.SetMode(HeadlessMode::Fast);
            bool scissorFastThrew = false;
            try { renderer.SetScissorRect(-1, -1, 4, 4); }
            catch (const HeadlessValidationException&) { scissorFastThrew = true; }
            check(!scissorFastThrew,
                  "HEADLESS-23: SetScissorRect() with a negative origin does not throw under HeadlessFast");

            renderer.SetMode(HeadlessMode::Validation);
        }

        // Checks I/J: HEADLESS-24, ApplySamplerState() with an out-of-range slot.
        {
            renderer.SetMode(HeadlessMode::Validation);
            bool samplerValidationThrew = false;
            try { renderer.ApplySamplerState(16, 0, 0, 0, 0); }
            catch (const HeadlessValidationException&) { samplerValidationThrew = true; }
            check(samplerValidationThrew,
                  "HEADLESS-24: ApplySamplerState() with slot=16 throws under HeadlessValidation");

            renderer.SetMode(HeadlessMode::Fast);
            bool samplerFastThrew = false;
            try { renderer.ApplySamplerState(16, 0, 0, 0, 0); }
            catch (const HeadlessValidationException&) { samplerFastThrew = true; }
            check(!samplerFastThrew,
                  "HEADLESS-24: ApplySamplerState() with slot=16 does not throw under HeadlessFast");

            renderer.SetMode(HeadlessMode::Validation);
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, 10);
        result_ = (passCount_ == 10) ? 0 : 1;
        Exit();
    }

public:
    HeadlessModeDialTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HeadlessModeDialTest game;
    game.Run();
    return game.getResult();
}
