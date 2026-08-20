// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-46: growing a persistent VertexBuffer/IndexBuffer mid-frame must not corrupt
// or crash an earlier, still-queued draw against that same buffer object's SMALLER, PREVIOUS
// content.
//
// Before LLGL-46, LlglVertexBufferRenderer::SetData()/LlglIndexBufferRenderer::Upload() released
// the buffer's existing LLGL::Buffer IMMEDIATELY and allocated a bigger one whenever the new
// upload exceeded the current byte capacity -- regardless of whether an earlier, not-yet-replayed
// FrameCommand still held that exact (now-freed) LLGL::Buffer* by raw pointer (frame commands only
// replay at Present()/FlushPendingFrameEXT() time, not when queued). That is a genuine
// use-after-free once the earlier draw finally replays, not merely a wrong-pixel bug: known_bugs.md
// documents a reverted fix attempt that reproduced exactly this shape of crash
// ("free(): invalid pointer" inside the Vulkan module) elsewhere in this renderer.
//
// This file draws a SMALL triangle from a VertexBuffer/IndexBuffer pair, queues that draw, THEN
// grows the SAME buffer objects (SetData() with more vertices/indices than were ever uploaded
// before) and queues a SECOND, differently-coloured, differently-positioned draw -- all before any
// Present()/GetData() flush. If the old buffer were freed early, the first draw's replay would
// either crash outright or (were the freed memory silently reused) show the second draw's content
// at the first draw's own position. Check A covers VertexBuffer growth, Check B covers IndexBuffer
// growth (a fixed 6-vertex VertexBuffer, only the IndexBuffer grows).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 320;
    constexpr int kHeight = 240;

    const Color kClear{0, 0, 0, 255};
    const Color kRed{255, 0, 0, 255};
    const Color kBlue{0, 0, 255, 255};

    std::vector<VertexPositionColor> Triangle(float left, float right, const Color& color)
    {
        return {
            VertexPositionColor(Vector3((left + right) * 0.5f, 30.0f, -0.5f), color),
            VertexPositionColor(Vector3(right, 200.0f, -0.5f), color),
            VertexPositionColor(Vector3(left, 200.0f, -0.5f), color),
        };
    }

    std::vector<VertexPositionColor> Quad(float left, float right, const Color& color)
    {
        const Vector3 topLeft(left, 40.0f, -0.5f);
        const Vector3 topRight(right, 40.0f, -0.5f);
        const Vector3 bottomLeft(left, 190.0f, -0.5f);
        const Vector3 bottomRight(right, 190.0f, -0.5f);
        return {
            VertexPositionColor(topLeft, color), VertexPositionColor(topRight, color),
            VertexPositionColor(bottomLeft, color), VertexPositionColor(bottomLeft, color),
            VertexPositionColor(topRight, color), VertexPositionColor(bottomRight, color),
        };
    }
}

class LlglVertexIndexBufferGrowTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> effect_;
    int logicalWidth_ = 0;
    int logicalHeight_ = 0;

    void SetUpEffect(GraphicsDevice& device)
    {
        auto& renderer = static_cast<CNA::Internal::Renderers::Llgl::LlglRenderer&>(
            device.GetRenderer());
        renderer.GetViewportSize(logicalWidth_, logicalHeight_);

        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setTextureEnabledProperty(false);
        effect_->setLightingEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth_), static_cast<float>(logicalHeight_),
            0.0f, 0.0f, 1.0f));
    }

public:
    LlglVertexIndexBufferGrowTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        SetUpEffect(device);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        // --- Check A: VertexBuffer grows mid-frame, IndexBuffer absent -------------------------
        device.Clear(kClear);
        {
            // Declared capacity 6 (a quad's worth); the renderer's own underlying LLGL::Buffer is
            // only ever sized from what SetData() ACTUALLY uploads, so the first, 3-vertex upload
            // below leaves it sized for 3 vertices -- the second, 6-vertex upload then genuinely
            // exceeds that and must grow.
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 6,
                                BufferUsage::None);

            const auto redTriangle = Triangle(20.0f, 140.0f, kRed);
            buffer.SetData(redTriangle.data(), static_cast<int>(redTriangle.size()));
            device.SetVertexBuffer(&buffer);
            effect_->Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);

            // Grows the SAME buffer object while the draw above is still queued (no Present/
            // GetData in between) -- before LLGL-46 this freed the buffer the queued draw above
            // still points at.
            const auto blueQuad = Quad(180.0f, 300.0f, kBlue);
            buffer.SetData(blueQuad.data(), static_cast<int>(blueQuad.size()));
            device.SetVertexBuffer(&buffer);
            effect_->Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

            ExpectPixel("A: the pre-grow triangle drew its OWN (red) content, not the grown quad's",
                        Rectangle(80, 100, 1, 1), kRed);
            ExpectPixel("A: the post-grow quad drew its OWN (blue) content at its own position",
                        Rectangle(240, 100, 1, 1), kBlue);
            ExpectPixel("A: nothing drew below both shapes", Rectangle(160, 225, 1, 1), kClear);
        }

        // --- Check B: IndexBuffer grows mid-frame, VertexBuffer fixed --------------------------
        device.Clear(kClear);
        {
            // A fixed 4-corner quad; only the INDEX buffer grows between the two draws.
            const std::vector<VertexPositionColor> corners{
                VertexPositionColor(Vector3(20.0f, 40.0f, -0.5f), kRed),
                VertexPositionColor(Vector3(140.0f, 40.0f, -0.5f), kRed),
                VertexPositionColor(Vector3(20.0f, 190.0f, -0.5f), kRed),
                VertexPositionColor(Vector3(140.0f, 190.0f, -0.5f), kRed),
                VertexPositionColor(Vector3(180.0f, 40.0f, -0.5f), kBlue),
                VertexPositionColor(Vector3(300.0f, 40.0f, -0.5f), kBlue),
                VertexPositionColor(Vector3(180.0f, 190.0f, -0.5f), kBlue),
                VertexPositionColor(Vector3(300.0f, 190.0f, -0.5f), kBlue),
            };
            VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                                      static_cast<int>(corners.size()), BufferUsage::None);
            vertexBuffer.SetData(corners.data(), static_cast<int>(corners.size()));

            // Declared capacity 6 (two triangles); the first upload below is only 3 indices (one
            // triangle, the RED corners 0-2), so the renderer's own buffer starts sized for 3
            // indices and must grow when the second upload asks for 6.
            IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            const std::vector<std::uint16_t> oneTriangle{0, 1, 2};
            indexBuffer.SetData(oneTriangle.data(), static_cast<int>(oneTriangle.size()));

            device.SetVertexBuffer(&vertexBuffer);
            device.setIndicesProperty(&indexBuffer);
            effect_->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                         static_cast<int>(corners.size()), 0, 1);

            // Grows the SAME index buffer object while the draw above is still queued -- indices
            // 4-7 (the BLUE quad's corners) reference vertices that were already uploaded above.
            const std::vector<std::uint16_t> twoTriangles{4, 5, 6, 6, 5, 7};
            indexBuffer.SetData(twoTriangles.data(), static_cast<int>(twoTriangles.size()));
            device.SetVertexBuffer(&vertexBuffer);
            device.setIndicesProperty(&indexBuffer);
            effect_->Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                         static_cast<int>(corners.size()), 0, 2);
            device.setIndicesProperty(nullptr);

            ExpectPixel("B: the pre-grow indexed triangle drew its OWN (red) content",
                        Rectangle(80, 100, 1, 1), kRed);
            ExpectPixel("B: the post-grow indexed quad drew its OWN (blue) content",
                        Rectangle(240, 100, 1, 1), kBlue);
            ExpectPixel("B: nothing drew below both shapes", Rectangle(160, 225, 1, 1), kClear);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglVertexIndexBufferGrowTest>();
}
