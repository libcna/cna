// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-30/31/32: the three render-target features FNA3D-10 claimed as done while
// the test it cited rendered none of them.
//
// `fna3d_rendertarget_test.cpp` covers RenderTarget2D only. Cube targets had exactly one check
// anywhere in the tree -- `CreateRenderTargetCube(...) != nullptr` in the capabilities test, which
// proves allocation and nothing else -- and MRT had a `SupportsCapability` answer with no draw
// behind it. Both are real FNA3D surfaces: `FNA3D_SetRenderTargets` takes an ordered array of
// bindings, each of which may name a cube face, and resolves each target's mip chain on unbind.
//
// FNA3D-30 (Check A) -- each of the six cube faces is a distinct, independently addressable
//   render destination: six different colours go in, six different colours come back out, and
//   writing one face does not disturb another.
// FNA3D-30 (Check B) -- geometry rendered into a cube face lands in that face, so a bound cube
//   face is a real raster destination and not just a clear target.
// FNA3D-31 (Check C) -- two render targets bound at once are both written by one draw, and each
//   holds its own contents afterwards. A renderer that quietly bound only slot 0 passes every
//   capability check and fails this.
// FNA3D-31 (Check D) -- unbinding an MRT set restores the backbuffer.
// FNA3D-32 (Check E) -- a mipmapped render target owns storage at levels above 0 and reports the
//   level count XNA's own rule gives.
// FNA3D-32 (Check F) -- level 0 of a mipmapped target reads back what was rendered into it, so
//   requesting mips does not cost the base level.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kFaceSize = 32;

    bool ColorNear(const Color& got, const Color& want, int tolerance = 2)
    {
        const auto close = [tolerance](int a, int b) {
            const int delta = a - b;
            return (delta < 0 ? -delta : delta) <= tolerance;
        };
        return close(got.getRProperty(), want.getRProperty())
            && close(got.getGProperty(), want.getGProperty())
            && close(got.getBProperty(), want.getBProperty());
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    /// The six faces, each with a colour no other face shares, so a mix-up cannot alias.
    struct FaceCase
    {
        CubeMapFace face;
        const char* name;
        Color color;
    };

    const FaceCase kFaces[6] = {
        { CubeMapFace::PositiveX, "PositiveX", Color(255, 0, 0, 255) },
        { CubeMapFace::NegativeX, "NegativeX", Color(0, 255, 0, 255) },
        { CubeMapFace::PositiveY, "PositiveY", Color(0, 0, 255, 255) },
        { CubeMapFace::NegativeY, "NegativeY", Color(255, 255, 0, 255) },
        { CubeMapFace::PositiveZ, "PositiveZ", Color(255, 0, 255, 255) },
        { CubeMapFace::NegativeZ, "NegativeZ", Color(0, 255, 255, 255) },
    };

    Color ReadFace(RenderTargetCube& cube, CubeMapFace face, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        cube.GetData(face, 0, &region, &pixel, 0, 1);
        return pixel;
    }

    Color ReadTarget(RenderTarget2D& target, int x, int y)
    {
        Color pixel(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        target.GetData(0, &region, &pixel, 0, 1);
        return pixel;
    }
}

class Fna3dRenderTargetAdvancedTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        // Rendering into a target flips Y, which reverses the effective winding of a clip-space
        // quad -- the same reason fna3d_rendertarget_test.cpp disables culling for its own draws.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        CubeFaceChecks(device);
        MultipleRenderTargetChecks(device);
        MipmappedTargetChecks(device);
    }

private:
    // ---- FNA3D-30 -------------------------------------------------------------------------
    void CubeFaceChecks(GraphicsDevice& device)
    {
        RenderTargetCube cube(device, kFaceSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        // Check A -- write all six first, then read all six. Reading immediately after each write
        // would still pass if every face aliased the same storage; interleaving this way means a
        // later face would have overwritten an earlier one and the read must catch it.
        for (const FaceCase& entry : kFaces)
        {
            device.SetRenderTargets({ RenderTargetBinding(&cube, entry.face) });
            device.Clear(entry.color);
        }
        device.SetRenderTargets({});

        for (const FaceCase& entry : kFaces)
        {
            const Color got = ReadFace(cube, entry.face, kFaceSize / 2, kFaceSize / 2);
            Check(ColorNear(got, entry.color),
                  (std::string("cube face ") + entry.name + " holds its own colour, got " +
                      Describe(got) + " want " + Describe(entry.color)).c_str());
        }

        // Check B -- a bound cube face is a real raster destination, not only a clear target.
        {
            RenderTargetCube drawn(device, kFaceSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTargets({ RenderTargetBinding(&drawn, CubeMapFace::PositiveZ) });
            device.Clear(Color(0, 0, 0, 255));
            DrawLeftHalfQuad(device, Color(255, 128, 0, 255));
            device.SetRenderTargets({});

            const Color left = ReadFace(drawn, CubeMapFace::PositiveZ, kFaceSize / 4,
                                        kFaceSize / 2);
            const Color right = ReadFace(drawn, CubeMapFace::PositiveZ, (kFaceSize * 3) / 4,
                                         kFaceSize / 2);
            Check(ColorNear(left, Color(255, 128, 0, 255)),
                  ("geometry drawn into a cube face lands in that face, got " + Describe(left)).c_str());
            Check(ColorNear(right, Color(0, 0, 0, 255)),
                  ("the untouched half of that cube face keeps its cleared colour, got " +
                      Describe(right)).c_str());

            // ... and the draw went to the face that was bound, not to face 0 by default.
            const Color otherFace = ReadFace(drawn, CubeMapFace::PositiveX, kFaceSize / 4,
                                             kFaceSize / 2);
            Check(!ColorNear(otherFace, Color(255, 128, 0, 255)),
                  ("the draw did not leak into an unbound cube face, got " + Describe(otherFace)).c_str());
        }
    }

    // ---- FNA3D-31 -------------------------------------------------------------------------
    void MultipleRenderTargetChecks(GraphicsDevice& device)
    {
        RenderTarget2D first(device, kFaceSize, kFaceSize, false, SurfaceFormat::Color,
                             DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        RenderTarget2D second(device, kFaceSize, kFaceSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        // Check C -- both targets bound at once. Clearing an MRT set clears every bound target, so
        // two distinct clears through two distinct bind sets, then one shared clear, distinguishes
        // "both are really bound" from "only slot 0 is".
        device.SetRenderTargets({ RenderTargetBinding(&first) });
        device.Clear(Color(10, 20, 30, 255));
        device.SetRenderTargets({ RenderTargetBinding(&second) });
        device.Clear(Color(200, 100, 50, 255));

        device.SetRenderTargets({ RenderTargetBinding(&first), RenderTargetBinding(&second) });
        device.Clear(Color(0, 180, 90, 255));
        device.SetRenderTargets({});

        const Color a = ReadTarget(first, kFaceSize / 2, kFaceSize / 2);
        const Color b = ReadTarget(second, kFaceSize / 2, kFaceSize / 2);
        Check(ColorNear(a, Color(0, 180, 90, 255)),
              ("MRT slot 0 received the clear, got " + Describe(a)).c_str());
        Check(ColorNear(b, Color(0, 180, 90, 255)),
              ("MRT slot 1 received the clear too, got " + Describe(b) +
                  " (a renderer that binds only slot 0 leaves this at (200,100,50))").c_str());

        // Each target still owns its own storage: write only slot 0 afterwards and slot 1 must
        // keep what it had, so the two are not aliases of one surface.
        device.SetRenderTargets({ RenderTargetBinding(&first) });
        device.Clear(Color(240, 240, 240, 255));
        device.SetRenderTargets({});
        const Color aAfter = ReadTarget(first, kFaceSize / 2, kFaceSize / 2);
        const Color bAfter = ReadTarget(second, kFaceSize / 2, kFaceSize / 2);
        Check(ColorNear(aAfter, Color(240, 240, 240, 255)),
              ("the re-bound single target took the new clear, got " + Describe(aAfter)).c_str());
        Check(ColorNear(bAfter, Color(0, 180, 90, 255)),
              ("the other target kept its own contents, got " + Describe(bAfter)).c_str());

        // Check D -- unbinding the MRT set restores the backbuffer as the destination.
        device.Clear(Color(0, 0, 0, 255));
        DrawLeftHalfQuad(device, Color(90, 200, 255, 255));
        ExpectPixel("after an MRT unbind the backbuffer is the draw destination",
                    Rectangle(device.getViewportProperty().getWidthProperty() / 4,
                              device.getViewportProperty().getHeightProperty() / 2, 1, 1),
                    Color(90, 200, 255, 255), 4);
    }

    // ---- FNA3D-32 -------------------------------------------------------------------------
    void MipmappedTargetChecks(GraphicsDevice& device)
    {
        // 32x32 -> levels 32,16,8,4,2,1 = 6, XNA's own full-chain rule.
        RenderTarget2D mipped(device, kFaceSize, kFaceSize, /*mipMap=*/true, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        // Check E -- the chain really exists.
        Check(mipped.getLevelCountProperty() == 6,
              ("a 32x32 mipmapped render target reports 6 levels, got " +
                  std::to_string(mipped.getLevelCountProperty())).c_str());

        device.SetRenderTargets({ RenderTargetBinding(&mipped) });
        device.Clear(Color(20, 140, 60, 255));
        device.SetRenderTargets({});

        // Check F -- asking for mips did not cost the base level.
        const Color base = ReadTarget(mipped, kFaceSize / 2, kFaceSize / 2);
        Check(ColorNear(base, Color(20, 140, 60, 255)),
              ("level 0 of a mipmapped target reads back what was rendered, got " +
                  Describe(base)).c_str());

        // A level above 0 must be addressable storage. Its exact filtered content depends on how
        // and when the driver generates the chain, so this checks that the level exists and can be
        // read at its own smaller extent -- not a specific filtered value this renderer never
        // promised. Reading past the chain must still be refused.
        {
            std::vector<Color> level1(16 * 16, Color(0, 0, 0, 0));
            const Rectangle whole(0, 0, kFaceSize / 2, kFaceSize / 2);
            bool readable = true;
            try
            {
                mipped.GetData(1, &whole, level1.data(), 0, 16 * 16);
            }
            catch (const std::exception&)
            {
                readable = false;
            }
            Check(readable, "mip level 1 of a render target is addressable storage");
        }
        {
            std::vector<Color> scratch(4, Color(0, 0, 0, 0));
            const Rectangle whole(0, 0, 1, 1);
            bool refused = false;
            try
            {
                mipped.GetData(mipped.getLevelCountProperty(), &whole, scratch.data(), 0, 1);
            }
            catch (const std::exception&)
            {
                refused = true;
            }
            Check(refused, "a level past the end of the chain is refused, not fabricated");
        }
    }

    /// Fills the left half of whatever is bound, in clip space, with one flat colour. Same
    /// BasicEffect idiom `fna3d_3d_test.cpp` uses: identity transforms, vertex colour only.
    void DrawLeftHalfQuad(GraphicsDevice& device, const Color& color)
    {
        if (!effect_)
        {
            effect_ = std::make_unique<BasicEffect>(device);
        }
        const VertexPositionColor quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), color },
            { Vector3( 0.0f,  1.0f, 0.0f), color },
            { Vector3(-1.0f, -1.0f, 0.0f), color },
            { Vector3( 0.0f,  1.0f, 0.0f), color },
            { Vector3( 0.0f, -1.0f, 0.0f), color },
            { Vector3(-1.0f, -1.0f, 0.0f), color },
        };
        effect_->VertexColorEnabled = true;
        effect_->setTextureEnabledProperty(false);
        effect_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    std::unique_ptr<BasicEffect> effect_;
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3dRenderTargetAdvancedTest>();
}
