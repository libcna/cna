// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1810: an imported glTF material through the engine layer, end to end.
//
// The engine layer's PBR and IBL work is verified against effects this repository constructs. This
// program checks the join everything else assumes: that a material which arrived through the
// *runtime glTF importer* -- `ContentManager::Load<Model>` on a `.gltf` file, no offline step --
// answers to the engine layer's image-based light and its shadow state the same way a hand-built
// `PbrEffect` does. If the importer ever produced an effect the layer could not reach, every other
// test here would still pass and no game would work.
//
// **Deviation from the row as written.** MOD-1810 asks for a glTF *character*. This repository has
// no character asset and deliberately so: `tests/assets/gltf/` is a 145-fixture *conformance*
// corpus whose largest mesh is six vertices, and vendoring an art asset to satisfy one demo would
// add a licence and a megabyte for no verification the fixtures cannot give. So this uses the
// corpus: a PBR fixture for the material path, and the skinned fixture for the skinned one. What
// is not shown is a character rig, and that is stated rather than implied.
//
// Check A -- the renderer rasterizes 3D, runs shader source, samples shadows and shades from an
//            environment, or the program SKIPs naming what it lacks.
// Check B -- the fixture loads through the runtime importer and its material arrives as a
//            `PbrEffect`, which is what makes the rest of this reachable at all.
// Check C -- binding an `ImageBasedLightEXT` to that imported effect changes what it renders, so
//            the engine layer's IBL reaches an imported material.
// Check D -- the imported model casts a shadow through `RenderPipeline::setShadowScene`.
// Check E -- the skinned fixture imports with `SkinningData` and its effect is a shadow receiver
//            too, so the skinned caster path is reachable from imported content.
// Check F -- with every setting off, the imported model renders pixel-identically to no pipeline.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/IShadowReceiverEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Content::ContentManager;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::GraphicsCapability;

#ifndef CNAEXT_GLTF_PBR_CORPUS
#define CNAEXT_GLTF_PBR_CORPUS "tests/assets/gltf"
#endif

namespace
{
    constexpr int kFrame = 128;

    /// A PBR fixture: opaque, metallic-rough, with a base-colour texture. The importer routes this
    /// through PbrEffect, which is the whole reason it is the one chosen here.
    constexpr const char* kPbrFixture     = "tex-reference-checkerboard";
    constexpr const char* kSkinnedFixture = "skin-four-weighted";

    int Luma(const Color& c)
    {
        return (c.getRProperty() * 299 + c.getGProperty() * 587 + c.getBProperty() * 114) / 1000;
    }

    double MeanLuma(const std::vector<Color>& frame)
    {
        double sum = 0.0;
        for (const Color& c : frame) sum += Luma(c);
        return frame.empty() ? 0.0 : sum / frame.size();
    }

    Matrix View()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 1.6f, 3.2f), Vector3::Zero, Vector3::Up);
    }

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 50.0f);
    }

    std::unique_ptr<TextureCube> MakeBrightCube(GraphicsDevice& device, int size)
    {
        auto cube = std::make_unique<TextureCube>(device, size, false, SurfaceFormat::Color);
        const std::vector<Color> face(static_cast<std::size_t>(size) * size,
                                      Color(255, 240, 220, 255));
        for (int i = 0; i < 6; ++i)
            cube->SetData(static_cast<CubeMapFace>(i), face.data(), static_cast<int>(face.size()));
        return cube;
    }
}

class GltfPbrExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::string corpus_ = CNAEXT_GLTF_PBR_CORPUS;
    int passCount_  = 0;
    int checkCount_ = 0;
    int result_     = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// The first PbrEffect the imported model carries, or null if the importer chose another one.
    static PbrEffect* FirstPbrEffect(Model& model)
    {
        for (const auto& mesh : model.getMeshesProperty())
            for (const auto& part : mesh->getMeshPartsProperty())
                if (auto* pbr = dynamic_cast<PbrEffect*>(part->getEffectProperty()))
                    return pbr;
        return nullptr;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

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

        // --- Check B: the runtime importer, and what it hands back -----------------------------
        ContentManager content(nullptr, corpus_);
        content.setGraphicsDevice(device);
        Model model = content.Load<Model>(kPbrFixture);
        PbrEffect* imported = FirstPbrEffect(model);
        std::printf("    %s: %d meshes, PbrEffect %s\n", kPbrFixture,
                    model.getMeshesProperty().getCountProperty(), imported ? "yes" : "no");
        check(imported != nullptr,
              "a PBR glTF material imports as a PbrEffect the engine layer can reach");
        if (imported == nullptr)
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
            result_ = 1;
            Exit();
            return;
        }

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        auto renderModel = [&](RenderPipeline* pipeline) {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            if (pipeline != nullptr) pipeline->begin(Color::Black);
            else                     device.Clear(Color::Black);
            model.Draw(Matrix::CreateScale(1.4f), View(), Projection());
            if (pipeline != nullptr) pipeline->end();
            device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        };

        // --- Check C: the engine layer's IBL reaches that imported material ---------------------
        const std::vector<Color> withoutIbl = renderModel(nullptr);

        EnvironmentProcessor processor(device);
        constexpr int kMips = 4;
        auto environmentCube = MakeBrightCube(device, 16);
        auto irradiance = processor.generateIrradiance(environmentCube.get(), 8, 8);
        auto specular   = processor.generatePrefilteredSpecular(environmentCube.get(), 8, kMips, 16);
        auto brdf       = processor.generateBrdfLut(32, 32);
        ImageBasedLightEXT environment;
        environment.Irradiance          = irradiance.get();
        environment.PrefilteredSpecular = specular.get();
        environment.BrdfLut             = brdf.get();
        environment.PrefilteredMipCount = kMips;
        imported->setImageBasedLightEXT(environment);

        const std::vector<Color> withIbl = renderModel(nullptr);
        std::printf("    imported material: mean luma %.1f without IBL, %.1f with\n",
                    MeanLuma(withoutIbl), MeanLuma(withIbl));
        check(MeanLuma(withIbl) > MeanLuma(withoutIbl) + 1.0,
              "an ImageBasedLightEXT bound to an imported material changes what it renders");

        // --- Check D: the imported model casts a shadow through the pipeline ---------------------
        RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        pipeline.getSettings().setShadowsEnabled(true);
        ShadowMap shadowMap(device, ShadowQuality::Medium);

        DirectionalLightEXT sun;
        sun.Direction = Vector3(-0.4f, -0.85f, -0.35f);
        const BoundingBox bounds(Vector3(-3.0f, -1.0f, -3.0f), Vector3(3.0f, 3.0f, 3.0f));
        pipeline.setShadowScene(&shadowMap, sun, bounds, [&] {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            model.Draw(Matrix::CreateScale(1.4f), View(), Projection());
        });
        renderModel(&pipeline);
        const bool passRan = pipeline.didShadowPassRun();

        // `didShadowPassRun()` says a pass happened, not that this model was in it -- a caster
        // callback that drew nothing would answer the same. So the map itself is compared against
        // the map produced by an empty callback: if drawing the imported model changes what is in
        // it, the model really was rasterized into the shadow map.
        std::size_t shadowTexels = 0;
        bool mapChanged = false;
        try
        {
            Texture2D* map = shadowMap.getShadowTexture();
            shadowTexels = static_cast<std::size_t>(map->getWidthProperty()) *
                           static_cast<std::size_t>(map->getHeightProperty());
            std::vector<Color> withModel(shadowTexels, Color::Transparent);
            map->GetData(withModel.data(), static_cast<int>(withModel.size()));

            pipeline.setShadowScene(&shadowMap, sun, bounds, [] {});
            renderModel(&pipeline);
            std::vector<Color> withNothing(shadowTexels, Color::Transparent);
            map->GetData(withNothing.data(), static_cast<int>(withNothing.size()));

            for (std::size_t i = 0; i < shadowTexels; ++i)
                if (!(withModel[i] == withNothing[i])) { mapChanged = true; break; }
            std::printf("    shadow map %zu texels; drawing the imported model changes it: %s\n",
                        shadowTexels, mapChanged ? "yes" : "no");
        }
        catch (const System::NotSupportedException&)
        {
            std::printf("    (shadow map not readable on this renderer -- falling back to the "
                        "pass-ran check alone)\n");
            mapChanged = passRan;
        }
        check(passRan && mapChanged,
              "the imported model is really rasterized into the shadow map by setShadowScene");

        // --- Check E: the skinned path is reachable from imported content too --------------------
        Model skinned = content.Load<Model>(kSkinnedFixture);
        auto* skinning = dynamic_cast<SkinningData*>(skinned.getTagProperty());
        bool skinnedReceives = false;
        for (const auto& mesh : skinned.getMeshesProperty())
            for (const auto& part : mesh->getMeshPartsProperty())
                if (dynamic_cast<IShadowReceiverEXT*>(part->getEffectProperty()) != nullptr)
                    skinnedReceives = true;
        std::printf("    %s: SkinningData %s, shadow-receiving effect %s\n", kSkinnedFixture,
                    skinning ? "yes" : "no", skinnedReceives ? "yes" : "no");
        check(skinning != nullptr && skinnedReceives,
              "a skinned fixture imports with SkinningData and a shadow-receiving effect");

        // --- Check F: inert is still inert with imported content ---------------------------------
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setShadowsEnabled(false);
        const std::vector<Color> inert  = renderModel(&pipeline);
        const std::vector<Color> direct = renderModel(nullptr);
        std::size_t differing = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (!(inert[i] == direct[i])) ++differing;
        std::printf("    inert pipeline vs no pipeline: %zu of %zu pixels differ\n", differing,
                    inert.size());
        check(differing == 0,
              "an imported model renders identically with an inert pipeline and without one");

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    GltfPbrExample(int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--content") == 0 && i + 1 < argc)
                corpus_ = argv[++i];

        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    GltfPbrExample example(argc, argv);
    example.Run();
    return example.result();
}
