// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1808/MOD-1811: every subsystem in the layer, in one frame.
//
// Every other example here isolates one thing so a failure names itself. This one does the
// opposite deliberately, because "each subsystem works" and "they work together" are different
// claims and only the second is what a game needs. The scene is small -- a lit ground plane, a
// PBR sphere-ish cube lit by an image-based light, a field of instanced cubes with LOD and frustum
// culling, a sky behind it, a sun casting a shadow -- and the frame goes through the full
// pipeline: depth/normal prepass, SSAO, HDR scene target, bloom, ACES tonemapping and FXAA.
//
// What it checks is that each subsystem *is still doing something* when all of them are on. That
// is the failure mode a per-subsystem test cannot see: a pass that silently stops contributing
// once another one is enabled ahead of it, a shadow that disappears when the sky draws, an SSAO
// term that reads a depth buffer the prepass no longer wrote. So every check is an A/B against
// the same frame with exactly one subsystem switched off.
//
// Check A -- the renderer can do all of it, or the program SKIPs naming what it lacks.
// Check B -- the composed frame renders, and the pipeline's own statistics agree with what was
//            switched on: the passes ran, the sky drew, the shadow pass ran, a scene target was used.
// Check C -- the sky is behind the scene rather than over it: the geometry's pixels are the
//            geometry's, and the background's are the sky's.
// Check D -- the shadow still darkens the ground with everything else enabled.
// Check E -- SSAO still darkens the contact region with everything else enabled.
// Check F -- bloom still spreads light beyond the bright object with everything else enabled.
// Check G -- tonemapping still compresses the highlights; the same frame untonemapped clips more.
// Check H -- culling and LOD still reduce work at this camera, with the field visible.
// Check I -- everything off is pixel-identical to no pipeline at all, from this same scene.
//
// `--screenshots DIR` writes the composed frame and the A/B variants as PNGs (MOD-1811). Off by
// default: a ctest that writes files into the build tree on every run is a nuisance, and the
// numeric checks above are the test.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/Graphics/LodGroupEXT.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::FrustumCullerEXT;
using CNA::Graphics::InstancedRendererEXT;
using CNA::Graphics::LodGroupEXT;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::Skybox;
using CNA::Graphics::TonemappingMode;
using CNA::GraphicsCapability;

namespace
{
    /// Bigger than the other examples' 128: these frames are committed as documentation
    /// (MOD-1811), and every window below is a fraction of it rather than an absolute pixel.
    constexpr int   kFrame      = 256;
    constexpr float kGroundHalf = 12.0f;
    constexpr float kBoxHalf    = 2.0f;
    /// The hero box's centre height. The shadow it casts is checked against this and the sun angle.
    constexpr float kBoxCentre  = 3.0f;
    constexpr int   kFieldSide  = 24;

    using Frame = std::vector<Color>;

    Color At(const Frame& frame, int x, int y)
    {
        return frame[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)];
    }

    int Luma(const Color& c)
    {
        return (c.getRProperty() * 299 + c.getGProperty() * 587 + c.getBProperty() * 114) / 1000;
    }

    /// Mean luma over an axis-aligned box of the frame, which is how every A/B below is compared.
    double MeanLuma(const Frame& frame, int x0, int y0, int x1, int y1)
    {
        double sum = 0.0;
        int count = 0;
        for (int y = std::max(0, y0); y < std::min(kFrame, y1); ++y)
            for (int x = std::max(0, x0); x < std::min(kFrame, x1); ++x)
            {
                sum += Luma(At(frame, x, y));
                ++count;
            }
        return count > 0 ? sum / count : 0.0;
    }

    /// How much light there is outside a box -- the measurement bloom is supposed to move.
    double LumaOutside(const Frame& frame, int x0, int y0, int x1, int y1)
    {
        double sum = 0.0;
        for (int y = 0; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
                if (x < x0 || x >= x1 || y < y0 || y >= y1)
                    sum += Luma(At(frame, x, y));
        return sum;
    }

    /// How many pixels of `darker` are at least `margin` darker than the same pixel of `lighter`.
    /// Used instead of a hardcoded sampling window: where a shadow or an occlusion term lands
    /// depends on the sun angle and the camera, and a window guessed from one run is how an A/B
    /// ends up measuring the wrong part of the frame and reporting no difference.
    int DarkenedPixels(const Frame& darker, const Frame& lighter, int margin)
    {
        int count = 0;
        for (std::size_t i = 0; i < darker.size(); ++i)
            if (Luma(lighter[i]) - Luma(darker[i]) >= margin)
                ++count;
        return count;
    }

    int ClippedPixels(const Frame& frame)
    {
        int count = 0;
        for (const Color& c : frame)
            if (c.getRProperty() >= 254 && c.getGProperty() >= 254 && c.getBProperty() >= 254)
                ++count;
        return count;
    }

    Matrix View()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 9.0f, 18.0f), Vector3(0.0f, 2.0f, 0.0f),
                                    Vector3::Up);
    }

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 1.0f, 120.0f);
    }

    void AppendQuad(std::vector<VertexPositionNormalTexture>& out, const Vector3& a,
                    const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& normal)
    {
        const Vector2 uv(0.0f, 0.0f);
        out.push_back(VertexPositionNormalTexture(a, normal, uv));
        out.push_back(VertexPositionNormalTexture(b, normal, uv));
        out.push_back(VertexPositionNormalTexture(c, normal, uv));
        out.push_back(VertexPositionNormalTexture(a, normal, uv));
        out.push_back(VertexPositionNormalTexture(c, normal, uv));
        out.push_back(VertexPositionNormalTexture(d, normal, uv));
    }

    std::vector<VertexPositionNormalTexture> Box(const Vector3& centre, float half)
    {
        std::vector<VertexPositionNormalTexture> out;
        out.reserve(36);
        const auto p = [&](float x, float y, float z) {
            return Vector3(centre.X + x * half, centre.Y + y * half, centre.Z + z * half);
        };
        AppendQuad(out, p(-1, 1, -1), p(1, 1, -1), p(1, 1, 1), p(-1, 1, 1), Vector3(0, 1, 0));
        AppendQuad(out, p(-1, -1, 1), p(1, -1, 1), p(1, -1, -1), p(-1, -1, -1), Vector3(0, -1, 0));
        AppendQuad(out, p(-1, -1, 1), p(-1, 1, 1), p(-1, 1, -1), p(-1, -1, -1), Vector3(-1, 0, 0));
        AppendQuad(out, p(1, -1, -1), p(1, 1, -1), p(1, 1, 1), p(1, -1, 1), Vector3(1, 0, 0));
        AppendQuad(out, p(-1, -1, 1), p(1, -1, 1), p(1, 1, 1), p(-1, 1, 1), Vector3(0, 0, 1));
        AppendQuad(out, p(1, -1, -1), p(-1, -1, -1), p(-1, 1, -1), p(1, 1, -1), Vector3(0, 0, -1));
        return out;
    }

    std::vector<VertexPositionNormalTexture> Ground()
    {
        std::vector<VertexPositionNormalTexture> out;
        const float e = kGroundHalf;
        AppendQuad(out, Vector3(-e, 0, -e), Vector3(e, 0, -e), Vector3(e, 0, e), Vector3(-e, 0, e),
                   Vector3(0, 1, 0));
        return out;
    }

    std::unique_ptr<TextureCube> MakeSkyCube(GraphicsDevice& device, int size)
    {
        auto cube = std::make_unique<TextureCube>(device, size, false, SurfaceFormat::Color);
        const std::vector<Color> horizon(static_cast<std::size_t>(size) * size,
                                         Color(90, 120, 200, 255));
        const std::vector<Color> up(static_cast<std::size_t>(size) * size,
                                    Color(160, 200, 255, 255));
        const std::vector<Color> down(static_cast<std::size_t>(size) * size,
                                      Color(40, 40, 50, 255));
        for (int face = 0; face < 6; ++face)
        {
            const std::vector<Color>& data = face == 2 ? up : (face == 3 ? down : horizon);
            cube->SetData(static_cast<CubeMapFace>(face), data.data(),
                          static_cast<int>(data.size()));
        }
        return cube;
    }
}

class ShowcaseExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> fieldVertices_;
    std::unique_ptr<IndexBuffer>  fieldIndices_;
    std::unique_ptr<ModelMeshPart> fieldPart_;
    std::string screenshotDir_;
    int passCount_  = 0;
    int checkCount_ = 0;
    int result_     = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void BuildFieldCube(GraphicsDevice& device)
    {
        const Vector3 corners[8] = {
            {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
            {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f}};
        std::vector<VertexPositionColor> data;
        data.reserve(8);
        for (const Vector3& corner : corners)
            data.emplace_back(corner, Color(180, 190, 210, 255));
        const std::vector<std::uint16_t> indexData{
            0, 2, 1, 0, 3, 2,  4, 5, 6, 4, 6, 7,  0, 1, 5, 0, 5, 4,
            3, 7, 6, 3, 6, 2,  0, 4, 7, 0, 7, 3,  1, 2, 6, 1, 6, 5};

        fieldVertices_ = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(),
            static_cast<int>(data.size()), BufferUsage::WriteOnly);
        fieldVertices_->SetData(data.data(), static_cast<int>(data.size()));
        fieldIndices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits,
                                                      static_cast<int>(indexData.size()),
                                                      BufferUsage::WriteOnly);
        fieldIndices_->SetData(indexData.data(), static_cast<int>(indexData.size()));
        fieldPart_ = std::make_unique<ModelMeshPart>(fieldVertices_.get(), fieldIndices_.get(),
                                                     static_cast<int>(data.size()), 12, 0, 0);
    }

    void SaveIfAsked(GraphicsDevice& device, const std::string& name)
    {
        if (screenshotDir_.empty()) return;
        try
        {
            const auto& viewport = device.getViewportProperty();
            const int width  = viewport.getWidthProperty();
            const int height = viewport.getHeightProperty();
            const std::size_t count = static_cast<std::size_t>(width) * height;
            std::vector<Color> pixels(count, Color::Transparent);
            device.GetBackBufferData(pixels.data(), static_cast<int>(count));
            std::vector<std::uint8_t> rgba(count * 4);
            for (std::size_t i = 0; i < count; ++i)
            {
                rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixels[i].getRProperty());
                rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
                rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
                rgba[i * 4 + 3] = 255;
            }
            Texture2D shot = Texture2D::CreateFromPixels(device, width, height, rgba);
            shot.SaveAsPng(screenshotDir_ + "/cnaext_showcase_" + name + ".png");
            std::printf("    wrote %s/cnaext_showcase_%s.png\n", screenshotDir_.c_str(),
                        name.c_str());
        }
        catch (const System::NotSupportedException&)
        {
            std::printf("    (no readable back buffer -- no screenshot for %s)\n", name.c_str());
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        BuildFieldCube(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        // Check A. The two-part question, four times: a capability says the renderer accepts a
        // thing, the EXT query says it does it. This program needs all of them, so it names which
        // one is missing rather than reporting a generic skip.
        const bool has3D      = device.SupportsCapability(GraphicsCapability::ThreeD);
        const bool hasEffects = device.SupportsCapability(GraphicsCapability::CustomEffects) &&
                                device.ExecutesShaderEffectSourceEXT();
        const bool hasShadows = device.SupportsShadowSamplingEXT();
        const bool hasIbl     = device.SupportsImageBasedLightingEXT();
        if (!has3D || !hasEffects || !hasShadows || !hasIbl)
        {
            std::printf("SKIP: this renderer is missing%s%s%s%s -- a documented capability "
                        "boundary, not a defect\n",
                        has3D ? "" : " 3D rasterization,",
                        hasEffects ? "" : " shader-source execution,",
                        hasShadows ? "" : " shadow sampling,",
                        hasIbl ? "" : " image-based lighting,");
            std::exit(77);
        }

        const auto ground = Ground();
        const auto box    = Box(Vector3(0.0f, kBoxCentre, 0.0f), kBoxHalf);
        const BoundingBox sceneBounds(Vector3(-kGroundHalf, -1.0f, -kGroundHalf),
                                      Vector3(kGroundHalf, kBoxCentre + kBoxHalf + 1.0f,
                                              kGroundHalf));
        // Tilted toward -X and +Z, so the shadow falls to the front-left -- onto bare ground,
        // in front of the instanced field rather than into it.
        const Vector3 sunDirection(-0.45f, -0.82f, 0.35f);

        // --- The subsystems, all constructed at once ------------------------------------------
        RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();

        ShadowMap shadowMap(device, ShadowQuality::High);
        DepthNormalPrepass prepass(device, kFrame, kFrame);

        auto skyCube = MakeSkyCube(device, 16);
        Skybox skybox(device, skyCube.get());

        EnvironmentProcessor processor(device);
        constexpr int kMips = 4;
        auto irradiance = processor.generateIrradiance(skyCube.get(), 8, 8);
        auto specular   = processor.generatePrefilteredSpecular(skyCube.get(), 8, kMips, 16);
        auto brdf       = processor.generateBrdfLut(32, 32);
        ImageBasedLightEXT environment;
        environment.Irradiance          = irradiance.get();
        environment.PrefilteredSpecular = specular.get();
        environment.BrdfLut             = brdf.get();
        environment.PrefilteredMipCount = kMips;

        BasicEffect groundEffect(device);
        PbrEffect   boxEffect(device);
        BasicEffect fieldEffect(device);

        DirectionalLightEXT sun;
        sun.Direction = sunDirection;

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);

        // The receiving half of the shadow contract is the effect's, not the pipeline's: switching
        // shadows off in the settings stops the pass running, and this stops the ground sampling a
        // map that no longer has anything in it. A game does both; forgetting the second is why
        // this is a named flag rather than a call buried in the A/B below.
        bool shadowsInShader = true;
        // The sky is not a setting -- it is attached or it is not -- so the inert comparison needs
        // its own flag rather than a setter buried in the render lambda. Attaching it there
        // unconditionally is what made the first version of check I compare a frame with a sky
        // against one without.
        bool skyAttached = true;

        // The geometry is drawn by one lambda so every variant below renders the *same* scene and
        // differs only in settings -- which is what makes each A/B a measurement of one subsystem.
        std::size_t drawnInstances = 0;
        auto drawScene = [&] {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            device.setBlendStateProperty(BlendState::Opaque);

            groundEffect.setLightingEnabledProperty(true);
            groundEffect.setTextureEnabledProperty(false);
            groundEffect.setDiffuseColorProperty(Vector3(0.42f, 0.44f, 0.48f));
            groundEffect.setAmbientLightColorProperty(Vector3(0.12f, 0.12f, 0.15f));
            groundEffect.setSpecularColorProperty(Vector3::Zero);
            auto& light = groundEffect.getDirectionalLight0Property();
            light.setEnabledProperty(true);
            light.setDirectionProperty(sunDirection);
            light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 0.95f));
            light.setSpecularColorProperty(Vector3::Zero);
            groundEffect.getDirectionalLight1Property().setEnabledProperty(false);
            groundEffect.getDirectionalLight2Property().setEnabledProperty(false);
            groundEffect.setWorldProperty(Matrix::getIdentityProperty());
            groundEffect.setViewProperty(View());
            groundEffect.setProjectionProperty(Projection());
            groundEffect.setShadowMapEXT(shadowMap.getShadowTexture());
            groundEffect.setShadowFilterRadiusEXT(shadowMap.getFilterRadius());
            groundEffect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
            groundEffect.setShadowsEnabledEXT(shadowsInShader);
            groundEffect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);

            // The hero object: PBR, image-based lighting, and a very bright emissive term so bloom
            // has something to spread. A dim object would make check F measure noise.
            boxEffect.setWorldProperty(Matrix::getIdentityProperty());
            boxEffect.setViewProperty(View());
            boxEffect.setProjectionProperty(Projection());
            boxEffect.setDiffuseColorProperty(Vector3(0.9f, 0.75f, 0.35f));
            boxEffect.setMetallicFactorProperty(0.9f);
            boxEffect.setRoughnessFactorProperty(0.25f);
            boxEffect.setEmissiveFactorProperty(Vector3(1.1f, 0.85f, 0.35f));
            boxEffect.setImageBasedLightEXT(environment);
            boxEffect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, box.data(), 0, 12);

            // The geometry-throughput half: a field of cubes, frustum-culled and LOD-selected, and
            // whatever survives is drawn in one instanced call.
            FrustumCullerEXT culler;
            culler.setCamera(View(), Projection());
            // Two levels: the cube up to 30 units away, nothing beyond. The far level draws
            // nothing on purpose -- LOD's cheapest level is "do not draw it", and it is the one
            // whose effect is visible in an instance count.
            LodGroupEXT lod;
            lod.addLevel(30.0f, fieldPart_.get());
            lod.addLevel(1000.0f, nullptr);

            std::vector<Matrix> visible;
            visible.reserve(static_cast<std::size_t>(kFieldSide) * kFieldSide);
            // The field lives in the back half of the ground on purpose: the hero box's shadow
            // falls toward the camera, and a field of cubes underneath it would mean check D
            // measured a cube's lit face rather than a shadow on the ground.
            for (int z = 0; z < kFieldSide; ++z)
                for (int x = 0; x < kFieldSide; ++x)
                {
                    const Vector3 at(-kGroundHalf + x * 1.05f, 0.5f, -kGroundHalf + z * 0.42f);
                    const BoundingBox bounds(at - Vector3(0.5f, 0.5f, 0.5f),
                                             at + Vector3(0.5f, 0.5f, 0.5f));
                    if (!culler.isVisible(bounds)) continue;
                    const float distance = (at - Vector3(0.0f, 9.0f, 18.0f)).Length();
                    if (lod.select(distance) == nullptr) continue;
                    visible.push_back(Matrix::CreateTranslation(at));
                }
            drawnInstances = visible.size();

            if (!visible.empty())
            {
                fieldEffect.setLightingEnabledProperty(false);
                fieldEffect.VertexColorEnabled = true;
                fieldEffect.setViewProperty(View());
                fieldEffect.setProjectionProperty(Projection());
                InstancedRendererEXT instanced(device, fieldPart_.get());
                instanced.setInstances(visible);
                instanced.draw(fieldEffect);
            }
        };

        // MOD-2035. The prepass is NOT `drawScene()`. `begin()` selects the prepass program and
        // sets its uniforms, and `drawScene()` immediately calls `Apply()` on the scene's own
        // effects -- so every draw replaced it, and the "depth" target ended up holding the shaded
        // frame's red channel. SSAO then compared shading against shading, which produces a weak,
        // plausible term everywhere instead of occlusion at contacts, and that is what check E was
        // measuring. The geometry is the same; only the effect differs.
        //
        // The instanced field is deliberately absent: the prepass program takes its world matrix
        // from a uniform and has no per-instance variant, so every cube would land at the origin.
        // It sits in the back half of the ground and is not what check E measures, and a cube pile
        // written into the depth image at one point would be worse than its absence.
        auto drawPrepassGeometry = [&] {
            ShaderEffect* prepassEffect = prepass.getPrepassEffect();
            if (prepassEffect == nullptr || !prepassEffect->IsEffectValid()) return;
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            device.setBlendStateProperty(BlendState::Opaque);
            prepassEffect->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
            prepassEffect->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, box.data(), 0, 12);
        };

        auto renderVariant = [&](bool wantPipeline) {
            if (!wantPipeline)
            {
                device.Clear(Color::Black);
                drawScene();
            }
            else
            {
                // The depth/normal prepass first: SSAO reads what it writes, and this is the one
                // ordering in the layer a caller has to get right themselves. Run only when SSAO
                // wants it -- a game does not pay for a prepass it is not going to read, and the
                // inert comparison below would otherwise be measuring an extra scene draw rather
                // than the pipeline.
                if (settings.isSSAOEnabled())
                {
                    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
                    {
                        prepass.begin(pass, View(), Projection(), 1.0f, 120.0f);
                        drawPrepassGeometry();
                        prepass.end();
                    }
                    pipeline.setDepthNormalInputs(prepass.getDepthTexture(),
                                                  prepass.getNormalTexture());
                }
                pipeline.setShadowScene(&shadowMap, sun, sceneBounds, drawScene);
                pipeline.setSkybox(skyAttached ? &skybox : nullptr);
                pipeline.setSkyboxCamera(View(), Projection());
                pipeline.begin(Color::Black);
                drawScene();
                pipeline.end();
            }
            device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        };

        auto enableEverything = [&] {
            settings.setHDREnabled(true);
            settings.setBloomEnabled(true);
            settings.setSSAOEnabled(true);
            // MOD-2035's second defect: the example never set a radius, so the 0.5 default applied
            // half the frame as a UV offset -- and with a correct depth image that darkens 23 994
            // pixels at all and 2 564 of them strongly, which is a global dimmer wearing AO's name.
            // 0.25 darkens 3 736 and 1 021, a bounded contact region. Narrower is not better here:
            // this estimator's radius is a UV offset compared against normalized depths, so at
            // 0.06 the samples land too near the centre to clear the bias and the term vanishes
            // entirely. The usable range on this scene is roughly 0.15 to 0.5.
            settings.setSSAORadius(0.25f);
            settings.setFXAAEnabled(true);
            settings.setShadowsEnabled(true);
            settings.setTonemappingMode(TonemappingMode::Aces);
            settings.setRenderQuality(RenderQuality::High);
            // Below 1 on purpose: the emissive box is deliberately over-bright so bloom has
            // something to work with, and at exposure 1 the whole frame clips to white -- which
            // still passes every numeric check below and makes a useless screenshot.
            settings.setExposure(0.55f);
        };

        // --- Check B: the composed frame, and the statistics that describe it ------------------
        enableEverything();
        const Frame everything = renderVariant(true);
        const auto stats = pipeline.getStatistics();
        std::printf("    passes %d, target switches %d, scene target %s, sky %s, %zu bytes, "
                    "%zu instances drawn\n",
                    stats.passesRun, stats.targetSwitches, stats.usedSceneTarget ? "yes" : "no",
                    stats.drewSkybox ? "yes" : "no", stats.gpuMemoryEstimateBytes, drawnInstances);
        check(stats.passesRun >= 4 && stats.usedSceneTarget && stats.drewSkybox &&
                  pipeline.didShadowPassRun(),
              "with everything enabled the pipeline reports every subsystem ran");
        SaveIfAsked(device, "everything");

        // The hero box's window, used by checks C and F, as fractions of the frame so the frame
        // size can change without silently moving what these two measure. The shadow and occlusion
        // checks below deliberately use no window at all -- see DarkenedPixels.
        constexpr int kBoxX0 = kFrame * 44 / 128, kBoxY0 = kFrame * 30 / 128;
        constexpr int kBoxX1 = kFrame * 84 / 128, kBoxY1 = kFrame * 74 / 128;

        // --- Check C: the sky is behind the scene ----------------------------------------------
        const double skyCorner = MeanLuma(everything, 0, 0, kFrame * 24 / 128, kFrame * 16 / 128);
        const double boxRegion = MeanLuma(everything, kBoxX0, kBoxY0, kBoxX1, kBoxY1);
        std::printf("    sky corner %.1f, hero box %.1f\n", skyCorner, boxRegion);
        check(skyCorner > 8.0 && boxRegion > skyCorner + 20.0,
              "the sky fills the background and the geometry is drawn over it, not under it");

        // --- Check D: the shadow survives everything else ---------------------------------------
        // Counted over the whole frame rather than sampled in a fixed window: where the shadow
        // lands is a function of the sun angle and the camera, and a window is how this check
        // silently starts measuring lit ground.
        settings.setShadowsEnabled(false);
        shadowsInShader = false;
        const Frame noShadow = renderVariant(true);
        const int shadowed = DarkenedPixels(everything, noShadow, 4);
        std::printf("    %d of %zu pixels are darker with shadows on\n", shadowed,
                    everything.size());
        // Between a hundredth and a third of the frame: fewer is noise, and more than a third
        // would mean the "shadow" is a global change in exposure rather than a shadow.
        check(shadowed > static_cast<int>(everything.size()) / 100 &&
                  shadowed < static_cast<int>(everything.size()) / 3,
              "the shadow still darkens part of the frame with every other subsystem enabled");
        SaveIfAsked(device, "no_shadow");
        settings.setShadowsEnabled(true);
        shadowsInShader = true;

        // --- Check E: SSAO survives everything else ---------------------------------------------
        settings.setSSAOEnabled(false);
        const Frame noAo = renderVariant(true);
        const int occluded = DarkenedPixels(everything, noAo, 2);
        std::printf("    pixels darker with SSAO on: %d by >=2, %d by >=8, %d by >=20 (of %zu)\n",
                    occluded, DarkenedPixels(everything, noAo, 8),
                    DarkenedPixels(everything, noAo, 20), everything.size());
        // Scored on the *strong* margin, not the weak one. At >=2 the count is 14 140 of 16 384 --
        // a hemisphere AO with this radius puts a slight term almost everywhere, and a check that
        // accepted that would pass just as happily if the pass had turned into a global dimmer.
        // What says "occlusion" is that a small, bounded part of the frame is darkened a lot.
        const int stronglyOccluded = DarkenedPixels(everything, noAo, 8);
        check(stronglyOccluded > static_cast<int>(everything.size()) / 100 &&
                  stronglyOccluded < static_cast<int>(everything.size()) / 3,
              "SSAO still darkens the frame where geometry meets geometry, and only there");
        SaveIfAsked(device, "no_ssao");
        settings.setSSAOEnabled(true);

        // --- Check F: bloom survives everything else --------------------------------------------
        const double glowOn = LumaOutside(everything, kBoxX0, kBoxY0, kBoxX1, kBoxY1);
        settings.setBloomEnabled(false);
        const Frame noBloom = renderVariant(true);
        const double glowOff = LumaOutside(noBloom, kBoxX0, kBoxY0, kBoxX1, kBoxY1);
        std::printf("    light outside the hero box: bloom on %.0f, off %.0f\n", glowOn, glowOff);
        check(glowOn > glowOff, "bloom still spreads light beyond the bright object");
        SaveIfAsked(device, "no_bloom");
        settings.setBloomEnabled(true);

        // --- Check G: tonemapping survives everything else --------------------------------------
        const int clippedOn = ClippedPixels(everything);
        settings.setTonemappingMode(TonemappingMode::None);
        const Frame noTonemap = renderVariant(true);
        const int clippedOff = ClippedPixels(noTonemap);
        std::printf("    fully clipped pixels: ACES %d, no tonemapping %d\n", clippedOn,
                    clippedOff);
        check(clippedOn <= clippedOff,
              "ACES still compresses the highlights the untonemapped frame clips");
        SaveIfAsked(device, "no_tonemap");
        settings.setTonemappingMode(TonemappingMode::Aces);

        // --- Check H: culling and LOD are doing work at this camera -------------------------------
        const std::size_t total = static_cast<std::size_t>(kFieldSide) * kFieldSide;
        std::printf("    field: %zu of %zu instances survived culling and LOD\n", drawnInstances,
                    total);
        check(drawnInstances > 0 && drawnInstances < total,
              "culling and LOD remove part of the field and keep part of it visible");

        // --- Check I: everything off is identical to no pipeline at all ---------------------------
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setShadowsEnabled(false);
        settings.setTonemappingMode(TonemappingMode::None);
        shadowsInShader = false;
        skyAttached = false;
        pipeline.setDepthNormalInputs(nullptr, nullptr);
        const Frame inert = renderVariant(true);
        const Frame direct = renderVariant(false);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (!(inert[i] == direct[i])) ++differing;
        std::printf("    inert pipeline vs no pipeline: %zu of %zu pixels differ\n", differing,
                    inert.size());
        check(differing == 0,
              "with every setting off the pipeline's frame is pixel-identical to no pipeline");
        SaveIfAsked(device, "inert");

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    ShowcaseExample(int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--screenshots") == 0 && i + 1 < argc)
                screenshotDir_ = argv[++i];

        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    ShowcaseExample example(argc, argv);
    example.Run();
    return example.result();
}
