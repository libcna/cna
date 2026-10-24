// SPDX-License-Identifier: MS-PL
// RenderTargetCube faces bound as simultaneous render targets receive their own pixel-shader
// outputs, and SetRenderTargets refuses the sets XNA refuses. Moved from the retired VULKAN suite
// (VKPAR-0025).
//
// The writer is an ordinary compiled XNA Effect (the shared synthetic Effect Framework fixture from
// tests/support) whose pixel shader writes oC0 = Tint, oC1 = Tint.yxzw, oC2 = Tint.zxyw and
// oC3 = Tint.zyxw, so every slot has a distinct, known colour. All four slots are always bound.
// Faces are read back with RenderTargetCube.GetData, and the faces NOT bound must keep the colour
// every face was cleared to first.
//
//   M0 control: four RenderTarget2D slots receive oC0..oC3 (the effect and readback work);
//   M1 a cube face (+Y) in slot 0 beside 2D targets: each slot holds its own output, the cube's
//      other faces are untouched;
//   M2 faces of two cubes (+Y of A in slot 0, -Z of B in slot 1);
//   M3 +X of one cube and -X of another, then ColorWriteChannels (Red) / ColorWriteChannels1 (Blue)
//      stay aligned with those two face slots;
//   M4 multisampled cube + multisampled 2D targets: an opaque triangle resolves to the exact output
//      inside and to intermediate coverage on its edge in the cube face AND in a 2D slot
//      (skipped when the device clamps the multisample request to one sample);
//   R1 the same cube face in two slots is an ArgumentException;
//   R2 two DIFFERENT faces of one cube are refused too -- XNA compares the resource, not the face --
//      and nothing is bound afterwards;
//   R3 a multisampled cube beside a single-sample 2D target is refused (when the applied counts
//      actually differ);
//   R4 a cube beside a 2D target of another size is refused.
//
// M-legs need GraphicsCapability::CompiledEffects; everything needs MultipleRenderTargets and a
// cube render target the renderer can bind and read back. Missing capabilities are [SKIP]s; when
// nothing could run the program exits 77.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = nothing could run on this renderer.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechniqueCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;
    const Color kFaceInit(3, 5, 7, 255);
    const Color kClear(10, 20, 30, 255);
    const Vector4 kTint(0.8f, 0.4f, 0.2f, 1.0f);
    /// oC0..oC3 for kTint: Tint, Tint.yxzw, Tint.zxyw, Tint.zyxw.
    const std::array<Color, 4> kOutputs = {
        Color(204, 102, 51, 255), Color(102, 204, 51, 255),
        Color(51, 204, 102, 255), Color(51, 102, 204, 255),
    };

    bool Near(const Color& got, const Color& want, int tolerance = 3)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance &&
               std::abs(got.getGProperty() - want.getGProperty()) <= tolerance &&
               std::abs(got.getBProperty() - want.getBProperty()) <= tolerance;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    struct ClipVertex { float x, y, z; };
}

class CubeMrtBindingTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Effect> mrt_;
    bool done_ = false;
    int result_ = 0;
    int ranChecks_ = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (!ok) result_ = 1;
        ++ranChecks_;
    }

    std::unique_ptr<RenderTarget2D> Make2D(int samples, int size = kSize)
    {
        return std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), size, size, false,
                                                SurfaceFormat::Color, DepthFormat::None, samples,
                                                RenderTargetUsage::PreserveContents);
    }

    /// A cube whose six faces are all cleared to kFaceInit first.
    std::unique_ptr<RenderTargetCube> MakeCube(int samples)
    {
        auto& dev = getGraphicsDeviceProperty();
        auto cube = std::make_unique<RenderTargetCube>(dev, kSize, false, SurfaceFormat::Color,
                                                       DepthFormat::None, samples,
                                                       RenderTargetUsage::PreserveContents);
        for (int face = 0; face < 6; ++face)
        {
            dev.SetRenderTarget(cube.get(), static_cast<CubeMapFace>(face));
            dev.Clear(kFaceInit);
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return cube;
    }

    static std::vector<Color> ReadFace(RenderTargetCube& cube, CubeMapFace face)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color(0, 0, 0, 0));
        cube.GetData(face, pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    static std::vector<Color> Read2D(RenderTarget2D& target)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color(0, 0, 0, 0));
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    static Color Centre(const std::vector<Color>& pixels)
    {
        return pixels[static_cast<std::size_t>((kSize / 2) * kSize + kSize / 2)];
    }

    /// Clears the four bound slots to kClear and draws the MRT effect over a full quad, or over the
    /// upper-left triangle when @p triangle is set.
    void DrawMrt(const std::vector<RenderTargetBinding>& bindings, const BlendState& blend,
                 bool triangle = false)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetRenderTargets(bindings);
        dev.Clear(kClear);
        dev.setBlendStateProperty(blend);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        auto& parameters = mrt_->getParametersProperty();
        parameters["Transform"]->SetValue(Matrix::getIdentityProperty());
        parameters["Tint"]->SetValue(kTint);
        mrt_->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
        const VertexDeclaration declaration(static_cast<int>(sizeof(ClipVertex)), {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        });
        const ClipVertex quad[6] = {
            {-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
            {-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f},
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad), 0,
                               triangle ? 1 : 2, declaration);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void CheckUntouched(RenderTargetCube& cube, CubeMapFace face, const std::string& label)
    {
        const Color got = Centre(ReadFace(cube, face));
        Check(Near(got, kFaceInit), label + " keeps its initial clear " + Show(got));
    }

    /// Runs one rendering leg; an exception is that leg's FAIL, not the end of the program. A
    /// set the renderer refuses by name (NotSupportedException -- WEBGPU and DIRECTX9 bind no
    /// cube face among several targets) is a missing capability, so a [SKIP], as the header says.
    template <typename Fn>
    void Leg(const char* name, Fn&& fn)
    {
        try
        {
            fn();
        }
        catch (const System::NotSupportedException& e)
        {
            getGraphicsDeviceProperty().SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::printf("[SKIP] %s: the renderer refuses the set: %s\n", name, e.what());
        }
        catch (const std::exception& e)
        {
            getGraphicsDeviceProperty().SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(false, std::string(name) + " threw instead of rendering: " + e.what());
        }
    }

    void RunRenderingLegs()
    {
        // ---- M0: control, four 2D targets -- the effect, its outputs and the readback path ------
        Leg("M0", [&] {
            auto a = Make2D(0), b = Make2D(0), c = Make2D(0), d = Make2D(0);
            DrawMrt({RenderTargetBinding(a.get()), RenderTargetBinding(b.get()),
                     RenderTargetBinding(c.get()), RenderTargetBinding(d.get())},
                    BlendState::Opaque);
            const Color s0 = Centre(Read2D(*a)), s1 = Centre(Read2D(*b));
            const Color s2 = Centre(Read2D(*c)), s3 = Centre(Read2D(*d));
            Check(Near(s0, kOutputs[0]) && Near(s1, kOutputs[1]) && Near(s2, kOutputs[2]) &&
                      Near(s3, kOutputs[3]),
                  "M0 control: four RenderTarget2D slots receive oC0..oC3: " + Show(s0) + " " +
                      Show(s1) + " " + Show(s2) + " " + Show(s3));
        });

        // ---- M1: one cube face beside three 2D targets ------------------------------------------
        Leg("M1", [&] {
            auto cube = MakeCube(0);
            auto a = Make2D(0), b = Make2D(0), c = Make2D(0);
            DrawMrt({RenderTargetBinding(cube.get(), CubeMapFace::PositiveY),
                     RenderTargetBinding(a.get()), RenderTargetBinding(b.get()),
                     RenderTargetBinding(c.get())},
                    BlendState::Opaque);
            const Color face = Centre(ReadFace(*cube, CubeMapFace::PositiveY));
            const Color s1 = Centre(Read2D(*a)), s2 = Centre(Read2D(*b)), s3 = Centre(Read2D(*c));
            Check(Near(face, kOutputs[0]) && Near(s1, kOutputs[1]) && Near(s2, kOutputs[2]) &&
                      Near(s3, kOutputs[3]),
                  "M1 cube +Y / 2D / 2D / 2D receive oC0..oC3: " + Show(face) + " " + Show(s1) +
                      " " + Show(s2) + " " + Show(s3));
            CheckUntouched(*cube, CubeMapFace::NegativeY, "M1 the cube's unbound -Y face");
            CheckUntouched(*cube, CubeMapFace::PositiveX, "M1 the cube's unbound +X face");
        });

        // ---- M2: faces of two cubes --------------------------------------------------------------
        Leg("M2", [&] {
            auto cubeA = MakeCube(0), cubeB = MakeCube(0);
            auto c = Make2D(0), d = Make2D(0);
            DrawMrt({RenderTargetBinding(cubeA.get(), CubeMapFace::PositiveY),
                     RenderTargetBinding(cubeB.get(), CubeMapFace::NegativeZ),
                     RenderTargetBinding(c.get()), RenderTargetBinding(d.get())},
                    BlendState::Opaque);
            const Color a = Centre(ReadFace(*cubeA, CubeMapFace::PositiveY));
            const Color b = Centre(ReadFace(*cubeB, CubeMapFace::NegativeZ));
            Check(Near(a, kOutputs[0]) && Near(b, kOutputs[1]),
                  "M2 cube A +Y gets oC0 and cube B -Z gets oC1: " + Show(a) + " / " + Show(b));
            CheckUntouched(*cubeA, CubeMapFace::NegativeZ, "M2 cube A's unbound -Z face");
            CheckUntouched(*cubeB, CubeMapFace::PositiveY, "M2 cube B's unbound +Y face");
        });

        // ---- M3: +X and -X of two cubes, then per-slot write masks ---------------------------------
        Leg("M3", [&] {
            auto one = MakeCube(0), other = MakeCube(0);
            auto c = Make2D(0), d = Make2D(0);
            const std::vector<RenderTargetBinding> faces = {
                RenderTargetBinding(one.get(), CubeMapFace::PositiveX),
                RenderTargetBinding(other.get(), CubeMapFace::NegativeX),
                RenderTargetBinding(c.get()), RenderTargetBinding(d.get())};
            DrawMrt(faces, BlendState::Opaque);
            const Color px = Centre(ReadFace(*one, CubeMapFace::PositiveX));
            const Color nx = Centre(ReadFace(*other, CubeMapFace::NegativeX));
            Check(Near(px, kOutputs[0]) && Near(nx, kOutputs[1]),
                  "M3 +X and -X of two cubes are distinct MRT slots: " + Show(px) + " / " +
                      Show(nx));

            BlendState masks = BlendState::Opaque;
            masks.setColorWriteChannelsProperty(ColorWriteChannels::Red);
            masks.setColorWriteChannels1Property(ColorWriteChannels::Blue);
            DrawMrt(faces, masks);
            const Color mpx = Centre(ReadFace(*one, CubeMapFace::PositiveX));
            const Color mnx = Centre(ReadFace(*other, CubeMapFace::NegativeX));
            const Color wantX(204, 20, 30, 255);   // oC0's red over the clear's green and blue
            const Color wantNx(10, 20, 51, 255);   // the clear's red and green under oC1's blue
            Check(Near(mpx, wantX) && Near(mnx, wantNx),
                  "M3 ColorWriteChannels Red / ColorWriteChannels1 Blue stay aligned with the +X / "
                  "-X face slots: " + Show(mpx) + " (expected " + Show(wantX) + ") / " + Show(mnx) +
                      " (expected " + Show(wantNx) + ")");
        });

        // ---- M4: multisampled cube MRT ------------------------------------------------------------
        Leg("M4", [&] {
            auto cube = MakeCube(8);
            auto a = Make2D(8), b = Make2D(8), c = Make2D(8);
            const int applied = cube->getMultiSampleCountProperty();
            if (applied <= 1 || a->getMultiSampleCountProperty() != applied)
            {
                std::printf("[SKIP] M4 multisampled cube MRT: applied sample counts cube=%d 2D=%d\n",
                            applied, a->getMultiSampleCountProperty());
                return;
            }
            DrawMrt({RenderTargetBinding(cube.get(), CubeMapFace::NegativeZ),
                     RenderTargetBinding(a.get()), RenderTargetBinding(b.get()),
                     RenderTargetBinding(c.get())},
                    BlendState::Opaque, /*triangle=*/true);
            const auto judge = [](const std::vector<Color>& pixels, const Color& output,
                                  int& inside, int& edge) {
                inside = 0;
                edge = 0;
                for (const Color& p : pixels)
                {
                    if (Near(p, output)) ++inside;
                    else if (!Near(p, kClear)) ++edge;
                }
            };
            int faceInside = 0, faceEdge = 0, rtInside = 0, rtEdge = 0;
            judge(ReadFace(*cube, CubeMapFace::NegativeZ), kOutputs[0], faceInside, faceEdge);
            judge(Read2D(*a), kOutputs[1], rtInside, rtEdge);
            const int half = kSize * kSize / 2;
            Check(faceInside > half - 2 * kSize && faceEdge > 0 && faceEdge <= 2 * kSize,
                  "M4 " + std::to_string(applied) + "x cube -Z face resolves oC0 inside (" +
                      std::to_string(faceInside) + " px) and coverage blends on the edge (" +
                      std::to_string(faceEdge) + " px)");
            Check(rtInside > half - 2 * kSize && rtEdge > 0 && rtEdge <= 2 * kSize,
                  "M4 the multisampled 2D slot 1 resolves oC1 inside (" +
                      std::to_string(rtInside) + " px) and coverage blends on the edge (" +
                      std::to_string(rtEdge) + " px)");
            CheckUntouched(*cube, CubeMapFace::PositiveZ, "M4 the multisampled cube's unbound +Z face");
        });
    }

    template <typename Fn>
    static bool RefusedWithArgumentException(Fn&& fn)
    {
        try
        {
            fn();
        }
        catch (const System::ArgumentException&)
        {
            return true;
        }
        return false;
    }

    void RunRejectionLegs()
    {
        auto& dev = getGraphicsDeviceProperty();
        auto cube = MakeCube(0);
        auto other = Make2D(0);

        const bool duplicate = RefusedWithArgumentException([&] {
            dev.SetRenderTargets({RenderTargetBinding(cube.get(), CubeMapFace::PositiveX),
                                  RenderTargetBinding(cube.get(), CubeMapFace::PositiveX)});
        });
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(duplicate, "R1 the same cube face in two slots is an ArgumentException");

        const bool twoFaces = RefusedWithArgumentException([&] {
            dev.SetRenderTargets({RenderTargetBinding(cube.get(), CubeMapFace::PositiveX),
                                  RenderTargetBinding(cube.get(), CubeMapFace::NegativeX)});
        });
        const bool nothingBound = dev.GetRenderTargets().empty();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(twoFaces && nothingBound,
              "R2 two different faces of ONE cube are refused as one resource in two slots, and "
              "nothing is bound afterwards");

        auto msaaCube = MakeCube(8);
        if (msaaCube->getMultiSampleCountProperty() == other->getMultiSampleCountProperty())
        {
            std::printf("[SKIP] R3 mismatched sample counts: the device applied %d samples to "
                        "the multisampled cube, the same as the single-sample target\n",
                        msaaCube->getMultiSampleCountProperty());
        }
        else
        {
            const bool samples = RefusedWithArgumentException([&] {
                dev.SetRenderTargets({RenderTargetBinding(msaaCube.get(), CubeMapFace::PositiveX),
                                      RenderTargetBinding(other.get())});
            });
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(samples, "R3 a " + std::to_string(msaaCube->getMultiSampleCountProperty()) +
                               "x cube beside a single-sample 2D target is an ArgumentException");
        }

        auto small = Make2D(0, kSize / 2);
        const bool extent = RefusedWithArgumentException([&] {
            dev.SetRenderTargets({RenderTargetBinding(cube.get(), CubeMapFace::PositiveX),
                                  RenderTargetBinding(small.get())});
        });
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(extent, "R4 a cube face beside a 2D target of another size is an ArgumentException");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        if (!dev.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        {
            std::printf("[SKIP] GraphicsCapability::MultipleRenderTargets is false\n");
            Exit();
            return;
        }
        try
        {
            auto probe = MakeCube(0);
            (void)ReadFace(*probe, CubeMapFace::PositiveX);
        }
        catch (const System::NotSupportedException& e)
        {
            std::printf("[SKIP] this renderer cannot bind and read back a RenderTargetCube: %s\n",
                        e.what());
            Exit();
            return;
        }

        RunRejectionLegs();

        if (!dev.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        {
            std::printf("[SKIP] M1-M4: GraphicsCapability::CompiledEffects is false, so no "
                        "multi-output pixel shader can be run\n");
        }
        else
        {
            mrt_ = std::make_unique<Effect>(dev, CNA::TestSupport::BuildSyntheticMrtDrawableEffect());
            RunRenderingLegs();
            mrt_.reset();
        }
        Exit();
    }

public:
    CubeMrtBindingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(96);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const
    {
        if (result_ != 0) return 1;
        return ranChecks_ == 0 ? CNA::Examples::kSkipExitCode : 0;
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("cube_mrt_binding"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    CubeMrtBindingTest game;
    game.Run();
    return game.getResult();
}
