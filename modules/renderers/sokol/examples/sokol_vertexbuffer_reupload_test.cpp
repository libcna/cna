// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-24: VertexBuffer/IndexBuffer SetData() used to unconditionally destroy and
// recreate the underlying sokol_gfx buffer on every upload. Both SokolVertexBufferRenderer and
// SokolIndexBufferRenderer now allocate a dynamic_update buffer once (sized to the owning
// VertexBuffer/IndexBuffer's own declared capacity) and reuse it in place via sg_update_buffer()
// for same-shape re-uploads across frames, only recreating when the data outgrows what is
// allocated or a second upload lands within the same frame (sokol_gfx allows only one
// sg_update_buffer() per buffer per frame).
//
// This test proves both halves of that contract with real, observable behaviour rather than just
// "the build still passes":
//
// Check A/B/C -- three consecutive frames, each calling SetData() once with a same-shaped but
//   differently-coloured quad, drawing and reading back the centre pixel each time. Every frame
//   must show the NEW colour (proving genuine re-upload, not stale GPU data), and
//   GetBufferIdEXT() must return the SAME sokol_gfx buffer id all three times (the actual
//   SOKOL-24 optimisation: proves sg_update_buffer() fired, not a silent recreate -- a test that
//   only checked pixel colour would pass identically against the old always-recreate code and
//   catch a future regression back to it).
// Check D -- two SetData() calls to the SAME VertexBuffer within one frame (no Present() between
//   them), which sokol_gfx's own once-per-buffer-per-frame update rule makes illegal to satisfy
//   with a second sg_update_buffer() -- this must fall back to recreating the buffer instead of
//   crashing, and the buffer id must differ from check C's (proving the forced recreate really
//   happened), with the final drawn colour matching the LAST SetData(), not a corrupted mix of
//   the two or the first (stale) one.
// Check E/F -- the same id-stability and cross-frame-correctness proof for IndexBuffer.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Sokol/SokolRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackBufferWidth = 320;
    constexpr int kBackBufferHeight = 240;

    // A 100x100 quad centred in the back buffer.
    const std::vector<std::uint16_t> kQuadIndices = { 0, 1, 2, 0, 2, 3 };

    std::vector<VertexPositionColor> MakeQuad(const Color& color)
    {
        return {
            { Vector3(110.0f, 70.0f, 0.0f), color },
            { Vector3(110.0f, 170.0f, 0.0f), color },
            { Vector3(210.0f, 170.0f, 0.0f), color },
            { Vector3(210.0f, 70.0f, 0.0f), color },
        };
    }

    Matrix PixelSpaceProjection()
    {
        return Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(kBackBufferWidth),
            static_cast<float>(kBackBufferHeight), 0.0f,
            -1.0f, 1.0f);
    }
}

class SokolVertexBufferReuploadTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<IndexBuffer> ib_;

    static std::uint32_t VbBufferId(VertexBuffer& vb)
    {
        return static_cast<CNA::Internal::Renderers::Sokol::SokolVertexBufferRenderer&>(
                   vb.GetRenderer())
            .GetBufferIdEXT();
    }

    static std::uint32_t IbBufferId(IndexBuffer& ib)
    {
        return static_cast<CNA::Internal::Renderers::Sokol::SokolIndexBufferRenderer&>(
                   ib.GetRenderer())
            .GetBufferIdEXT();
    }

    void DrawQuad(GraphicsDevice& device)
    {
        device.SetVertexBuffer(vb_.get());
        device.SetIndexBuffer(ib_.get());
        effect_->Apply();
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        device.SetDepthTestEnabled(false);
        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(rs);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(PixelSpaceProjection());

        const Rectangle centre(kBackBufferWidth / 2, kBackBufferHeight / 2, 1, 1);

        vb_ = std::make_unique<VertexBuffer>(device, VertexPositionColor::getVertexDeclarationStatic(),
                                              4, BufferUsage::None);
        ib_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 6,
                                             BufferUsage::None);

        // Check A -- first upload: creates both buffers.
        const auto red = MakeQuad(Color(255, 0, 0, 255));
        vb_->SetData(red.data(), static_cast<int>(red.size()));
        ib_->SetData(kQuadIndices.data(), static_cast<int>(kQuadIndices.size()));
        const std::uint32_t vbId1 = VbBufferId(*vb_);
        const std::uint32_t ibId1 = IbBufferId(*ib_);
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(device);
        ExpectPixel("A: frame 1 (red)", centre, Color(255, 0, 0, 255));
        device.Present();

        // Check B -- second frame, same shape, new colour: must reuse both buffer ids.
        const auto green = MakeQuad(Color(0, 255, 0, 255));
        vb_->SetData(green.data(), static_cast<int>(green.size()));
        ib_->SetData(kQuadIndices.data(), static_cast<int>(kQuadIndices.size()));
        const std::uint32_t vbId2 = VbBufferId(*vb_);
        const std::uint32_t ibId2 = IbBufferId(*ib_);
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(device);
        ExpectPixel("B: frame 2 (green)", centre, Color(0, 255, 0, 255));
        if (vbId2 == vbId1 && ibId2 == ibId1)
            std::printf("[PASS] B: VertexBuffer/IndexBuffer reused sg_update_buffer() (id "
                        "unchanged: vb=%u, ib=%u)\n", vbId2, ibId2);
        else
        {
            std::printf("[FAIL] B: expected buffer ids unchanged (cheap update path), got "
                        "vb %u->%u, ib %u->%u\n", vbId1, vbId2, ibId1, ibId2);
            MarkFailedEXT();
        }
        device.Present();

        // Check C -- third frame, same shape again: still the same ids.
        const auto blue = MakeQuad(Color(0, 0, 255, 255));
        vb_->SetData(blue.data(), static_cast<int>(blue.size()));
        ib_->SetData(kQuadIndices.data(), static_cast<int>(kQuadIndices.size()));
        const std::uint32_t vbId3 = VbBufferId(*vb_);
        const std::uint32_t ibId3 = IbBufferId(*ib_);
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(device);
        ExpectPixel("C: frame 3 (blue)", centre, Color(0, 0, 255, 255));
        if (vbId3 == vbId1 && ibId3 == ibId1)
            std::printf("[PASS] C: VertexBuffer/IndexBuffer still reusing the same buffer\n");
        else
        {
            std::printf("[FAIL] C: expected buffer ids still unchanged, got vb %u->%u, "
                        "ib %u->%u\n", vbId1, vbId3, ibId1, ibId3);
            MarkFailedEXT();
        }

        // Check D -- two SetData() calls to the SAME buffers within one frame (no Present()
        // since check C): sokol_gfx permits only one sg_update_buffer() per buffer per frame, so
        // this must fall back to recreating rather than crashing or corrupting data. The final
        // visible colour must be the LAST SetData()'s (white), and the id must have changed from
        // check C's (proving a real recreate happened, not a silently-skipped second update).
        const auto yellow = MakeQuad(Color(255, 255, 0, 255));
        vb_->SetData(yellow.data(), static_cast<int>(yellow.size()));
        const auto white = MakeQuad(Color(255, 255, 255, 255));
        vb_->SetData(white.data(), static_cast<int>(white.size()));
        const std::uint32_t vbId4 = VbBufferId(*vb_);
        device.Clear(Color(0, 0, 0, 255));
        DrawQuad(device);
        ExpectPixel("D: two SetData() calls in one frame draws the LAST one (white)", centre,
                    Color(255, 255, 255, 255));
        if (vbId4 != vbId3)
            std::printf("[PASS] D: same-frame repeat upload forced a real buffer recreate "
                        "(id changed: %u->%u)\n", vbId3, vbId4);
        else
        {
            std::printf("[FAIL] D: expected a new buffer id after a same-frame repeat upload, "
                        "still %u\n", vbId4);
            MarkFailedEXT();
        }
    }

public:
    SokolVertexBufferReuploadTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackBufferWidth);
        gdm_->setPreferredBackBufferHeightProperty(kBackBufferHeight);
    }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SokolVertexBufferReuploadTest game;
    game.Run();
    return game.getResultProperty();
}
