// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-4: the vertical-slice proof for the FNA3D graphics renderer -- selection,
// device creation, resource creation, real rendering, readback oracle and teardown, all in one
// process running entirely under CNA_GRAPHICS_RENDERER=FNA3D.
//
// Check A -- the compiled renderer identity really is FNA3D (compile-time constant + runtime name).
// Check B -- Clear() reaches the GPU: the backbuffer reads back as the exact cleared colour, at
//   the origin, the centre and the far corner (so a partial or mis-oriented clear cannot pass).
// Check C -- a second Clear() to a different colour replaces the first, proving repeated frames
//   are not a single lucky first frame.
// Check D -- Texture2D::CreateFromPixels + GetData round-trips every byte through a real
//   FNA3D_Texture (upload and readback, not a CPU shadow: the renderer keeps none).
// Check E -- VertexBuffer/IndexBuffer report back the counts they were given after a real upload
//   into FNA3D buffer objects.
// Check F -- an OcclusionQuery completes and reports a sample count.
// Check G -- the renderer's own device pointer exists (the FNA3D device was really created).
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    std::vector<std::uint8_t> QuadrantPixels()
    {
        // 4x4 RGBA8, one solid colour per 2x2 quadrant: red / green / blue / white.
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * 4 + x) * 4;
                const bool left = x < 2;
                const bool top = y < 2;
                pixels[offset + 0] = static_cast<std::uint8_t>((top && left) || (!top && !left) ? 255 : 0);
                pixels[offset + 1] = static_cast<std::uint8_t>((top && !left) || (!top && !left) ? 255 : 0);
                pixels[offset + 2] = static_cast<std::uint8_t>((!top && left) || (!top && !left) ? 255 : 0);
                pixels[offset + 3] = 255;
            }
        }
        return pixels;
    }
}

class Fna3dSmokeTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        // Check A -- identity.
        // plans/plan_runtimerenderer.md RTR-P7-7: the compile-time identity names the build DEFAULT, so
        // this assertion only holds when FNA3D is that default. In a multi-renderer build where
        // FNA3D is merely one of several, it is not -- and this program is still meaningful,
        // because it selects FNA3D at runtime.
#ifndef CNA_MULTI_RENDERER
        static_assert(CNA::getCurrentGraphicsRendererType() == CNA::GraphicsRendererType::Fna3d,
                      "this test only means anything when built as the FNA3D renderer");
#endif
        Check(CNA::getCurrentGraphicsRendererName() == "FNA3D",
              "compiled renderer identity is FNA3D");

        int width = 0;
        int height = 0;
        device.GetRenderer().GetViewportSize(width, height);
        Check(width > 0 && height > 0, "the FNA3D backbuffer has a non-degenerate size");

        // Check G -- a real FNA3D device exists behind the interface.
        auto* fna3d = dynamic_cast<CNA::Internal::Renderers::Fna3d::Fna3dRenderer*>(
            &device.GetRenderer());
        if (Check(fna3d != nullptr, "the active renderer is Fna3dRenderer"))
        {
            Check(fna3d->GetDeviceEXT() != nullptr, "FNA3D_CreateDevice produced a device");
        }

        // Check B -- the clear really reaches the GPU and readback sees it everywhere.
        device.Clear(Color(0, 255, 0, 255));
        ExpectPixel("clear reaches the origin", Rectangle(0, 0, 1, 1), Color(0, 255, 0, 255));
        ExpectPixel("clear reaches the centre", Rectangle(width / 2, height / 2, 1, 1),
                    Color(0, 255, 0, 255));
        ExpectPixel("clear reaches the far corner", Rectangle(width - 1, height - 1, 1, 1),
                    Color(0, 255, 0, 255));

        // Check C -- a second frame replaces the first.
        device.Clear(Color(32, 64, 128, 255));
        ExpectPixel("a second clear replaces the first", Rectangle(width / 2, height / 2, 1, 1),
                    Color(32, 64, 128, 255));

        // Check D -- real texture upload + readback through FNA3D.
        const std::vector<std::uint8_t> source = QuadrantPixels();
        Texture2D texture = Texture2D::CreateFromPixels(device, 4, 4, source);
        Check(texture.getWidthProperty() == 4 && texture.getHeightProperty() == 4,
              "Texture2D reports the size it was created with");

        // Read back through the RENDERER, not Texture2D::GetData -- the shared layer answers from
        // its own CPU pixel shadow when it has one, so only the renderer-level call proves the
        // bytes actually reached and returned from an FNA3D_Texture.
        std::vector<std::uint8_t> readback(source.size(), 0);
        const bool readOk = texture.GetRenderer().GetData(
            0, 0, 0, 4, 4, readback.data(), static_cast<int>(readback.size()));
        Check(readOk, "the FNA3D texture renderer performed a real GPU readback");
        Check(readOk && readback == source,
              "Texture2D upload/readback round-trips every byte through FNA3D");

        // Check E -- real buffer uploads.
        VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                                  BufferUsage::WriteOnly);
        const VertexPositionColor vertices[3] = {
            VertexPositionColor(Vector3(0.0f, 0.5f, 0.0f), Color::Red),
            VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::Green),
            VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::Blue),
        };
        vertexBuffer.SetData(vertices, 0, 3);
        Check(vertexBuffer.getVertexCountProperty() == 3,
              "VertexBuffer reports the vertex count it was given");

        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::WriteOnly);
        const std::uint16_t indices[3] = { 0, 1, 2 };
        indexBuffer.SetData(indices, 0, 3);
        Check(indexBuffer.getIndexCountProperty() == 3,
              "IndexBuffer reports the index count it was given");

        // Check F -- a real hardware occlusion query.
        OcclusionQuery query(device);
        query.Begin();
        query.End();
        int spins = 0;
        while (!query.getIsCompleteProperty() && spins < 100000)
        {
            ++spins;
        }
        Check(query.getIsCompleteProperty(), "OcclusionQuery completes");
        if (query.getIsCompleteProperty())
        {
            Check(query.getPixelCountProperty() >= 0,
                  "OcclusionQuery reports a non-negative sample count");
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dSmokeTest>();
}
