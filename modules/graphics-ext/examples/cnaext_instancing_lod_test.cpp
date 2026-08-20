// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1403/MOD-1410/MOD-1412/MOD-1413: instancing, LOD and culling together.
//
// A field of 10 000 cubes, three levels of detail, and a camera that can see about a third of it.
// The unit tests already pin each class's answers; what only a real device can show is that the
// three compose into a frame -- one draw call for ten thousand objects, a cull that removes the
// ones behind the camera before they are ever uploaded, and a measurable difference against the
// same scene drawn one call at a time.
//
// Check A -- the renderer rasters 3D and supports instancing, or the program SKIPs.
// Check B -- 10 000 instances reach the screen in ONE draw call.
// Check C -- culling drops the instances the camera cannot see, and the picture survives it.
// Check D -- the LOD group distributes the field across its three levels by distance.
//
// `--benchmark` times instanced against looped drawing (MOD-1413).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/Graphics/LodGroupEXT.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "System/NotSupportedException.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::FrustumCullerEXT;
using CNA::Graphics::InstancedRendererEXT;
using CNA::Graphics::LodGroupEXT;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kFrame = 128;
    constexpr int kFieldSide = 100;                       // 100 x 100 = 10 000 cubes
    constexpr int kInstances = kFieldSide * kFieldSide;
    constexpr float kSpacing = 3.0f;
}

class InstancingLodExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> vertices_;
    std::unique_ptr<IndexBuffer> indices_;
    std::unique_ptr<ModelMeshPart> part_;
    bool benchmark_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// A unit cube, as 12 triangles; the geometry is irrelevant beyond being visibly solid.
    void BuildCube(GraphicsDevice& device)
    {
        const Vector3 corners[8] = {
            {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
            {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},  {0.5f, 0.5f, 0.5f},  {-0.5f, 0.5f, 0.5f}};
        std::vector<VertexPositionColor> data;
        data.reserve(8);
        for (const Vector3& corner : corners)
            data.emplace_back(corner, Color(200, 220, 255, 255));

        const std::vector<std::uint16_t> indexData{
            0, 2, 1, 0, 3, 2,  4, 5, 6, 4, 6, 7,  0, 1, 5, 0, 5, 4,
            3, 7, 6, 3, 6, 2,  0, 4, 7, 0, 7, 3,  1, 2, 6, 1, 6, 5};

        vertices_ = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(),
            static_cast<int>(data.size()), BufferUsage::WriteOnly);
        vertices_->SetData(data.data(), static_cast<int>(data.size()));
        indices_ = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits,
                                                 static_cast<int>(indexData.size()),
                                                 BufferUsage::WriteOnly);
        indices_->SetData(indexData.data(), static_cast<int>(indexData.size()));
        part_ = std::make_unique<ModelMeshPart>(vertices_.get(), indices_.get(),
                                                static_cast<int>(data.size()), 12, 0, 0);
    }

    static Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 1.0f, 500.0f);
    }

    static Matrix View()
    {
        // Above one corner of the field, looking across it: part of the field is behind the camera,
        // which is what gives culling something to remove.
        return Matrix::CreateLookAt(Vector3(0.0f, 40.0f, 40.0f), Vector3(80.0f, 0.0f, -80.0f),
                                    Vector3(0.0f, 1.0f, 0.0f));
    }

    static std::vector<Matrix> Field()
    {
        std::vector<Matrix> transforms;
        transforms.reserve(kInstances);
        for (int z = 0; z < kFieldSide; ++z)
            for (int x = 0; x < kFieldSide; ++x)
                transforms.push_back(Matrix::CreateTranslation(
                    Vector3(static_cast<float>(x) * kSpacing, 0.0f,
                            -static_cast<float>(z) * kSpacing)));
        return transforms;
    }

    static std::vector<BoundingBox> FieldBounds(const std::vector<Matrix>& transforms)
    {
        std::vector<BoundingBox> bounds;
        bounds.reserve(transforms.size());
        for (const Matrix& transform : transforms)
        {
            const Vector3 centre(transform.M41, transform.M42, transform.M43);
            bounds.emplace_back(centre - Vector3(0.5f, 0.5f, 0.5f),
                                centre + Vector3(0.5f, 0.5f, 0.5f));
        }
        return bounds;
    }

    std::unique_ptr<BasicEffect> MakeEffect(GraphicsDevice& device)
    {
        auto effect = std::make_unique<BasicEffect>(device);
        effect->setWorldProperty(Matrix::getIdentityProperty());
        effect->setViewProperty(View());
        effect->setProjectionProperty(Projection());
        effect->setLightingEnabledProperty(false);
        effect->setTextureEnabledProperty(false);
        effect->VertexColorEnabled = true;
        return effect;
    }

    static int LitPixels(const std::vector<Color>& pixels)
    {
        int lit = 0;
        for (const Color& pixel : pixels)
            if (pixel.getRProperty() > 20 || pixel.getGProperty() > 20 || pixel.getBProperty() > 20)
                ++lit;
        return lit;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
        {
            std::printf("SKIP: this renderer does not raster 3D\n");
            std::exit(77);
        }
        BuildCube(device);
        InstancedRendererEXT renderer(device, part_.get());
        if (!renderer.isInstancingSupported())
        {
            std::printf("SKIP: this renderer does not support hardware instancing (a documented "
                        "capability boundary, not a defect)\n");
            std::exit(77);
        }

        auto effect = MakeEffect(device);
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        const auto readBack = [&] {
            try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const System::NotSupportedException&)
            {
                std::printf("SKIP: this renderer has no readable back buffer\n");
                std::exit(77);
            }
        };

        const std::vector<Matrix> field = Field();
        const std::vector<BoundingBox> bounds = FieldBounds(field);

        // --- B: ten thousand instances, one draw call ------------------------------------------
        device.Clear(Color::Black);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setBlendStateProperty(BlendState::Opaque);
        renderer.setInstances(field);
        renderer.draw(*effect);
        readBack();
        const int litAll = LitPixels(pixels);
        std::printf("    %d instances, %d draw call(s), %d lit pixels\n", renderer.getInstanceCount(),
                    renderer.getLastDrawCallCount(), litAll);
        check(renderer.getLastDrawCallCount() == 1 && renderer.didLastDrawInstance(),
              "10 000 instances render in a single draw call");
        check(litAll > 100, "the instanced field actually reached the screen");

        // --- C: culling first, then uploading only what survives --------------------------------
        FrustumCullerEXT culler;
        culler.setCamera(View(), Projection());
        std::vector<Matrix> visible;
        const std::size_t visibleCount = culler.cullTransforms(field, bounds, visible);
        std::printf("    culling kept %zu of %d instances\n", visibleCount, kInstances);
        renderer.setInstances(visible);
        device.Clear(Color::Black);
        renderer.draw(*effect);
        readBack();
        const int litVisible = LitPixels(pixels);
        std::printf("    culled draw: %d lit pixels against %d unculled\n", litVisible, litAll);
        check(visibleCount > 0 && visibleCount < static_cast<std::size_t>(kInstances),
              "culling removed the instances outside the frustum and kept the rest");
        // The same picture from fewer instances is the whole claim of culling; llvmpipe rasterises
        // deterministically, so this is an equality rather than a tolerance.
        check(litVisible == litAll, "culling did not change the picture, only the work");

        // --- D: the LOD group spreads the field over its levels ---------------------------------
        LodGroupEXT lod;
        lod.addLevel(60.0f, part_.get());
        lod.addLevel(150.0f, part_.get());
        lod.addLevel(400.0f, part_.get());
        int perLevel[4] = {0, 0, 0, 0};   // three levels plus "too far to draw"
        const Vector3 eye(0.0f, 40.0f, 40.0f);
        for (const Matrix& transform : field)
        {
            const Vector3 centre(transform.M41, transform.M42, transform.M43);
            const float distance = (centre - eye).Length();
            const int index = lod.selectIndex(distance);
            ++perLevel[index < 0 ? 3 : index];
        }
        std::printf("    LOD levels: near %d, middle %d, far %d, beyond %d\n", perLevel[0],
                    perLevel[1], perLevel[2], perLevel[3]);
        check(perLevel[0] > 0 && perLevel[1] > 0 && perLevel[2] > 0,
              "the field is distributed across all three levels of detail");

        if (benchmark_)
        {
            const auto timeOf = [&](int instances, bool instanced) {
                std::vector<Matrix> subset(field.begin(), field.begin() + instances);
                renderer.setInstances(subset);
                renderer.setFallbackEnabled(!instanced);
                const auto frame = [&] {
                    device.Clear(Color::Black);
                    if (instanced)
                    {
                        renderer.draw(*effect);
                    }
                    else
                    {
                        // The naive path, written out rather than routed through the fallback, so
                        // the comparison is against what a game would actually have written.
                        device.SetVertexBuffer(part_->getVertexBufferProperty());
                        device.setIndicesProperty(part_->getIndexBufferProperty());
                        for (const Matrix& transform : subset)
                        {
                            effect->setWorldProperty(transform);
                            effect->Apply();
                            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0,
                                                         part_->getNumVerticesProperty(), 0, 12);
                        }
                        effect->setWorldProperty(Matrix::getIdentityProperty());
                    }
                };
                frame();
                const auto start = std::chrono::steady_clock::now();
                constexpr int kFrames = 10;
                for (int i = 0; i < kFrames; ++i) frame();
                return std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - start).count() / kFrames;
            };

            std::printf("--- MOD-1413: 128x128, Mesa llvmpipe ---\n");
            for (const int count : {1000, 10000})
            {
                const double instanced = timeOf(count, true);
                const double looped    = timeOf(count, false);
                std::printf("    %5d cubes: instanced %8.3f ms  looped %8.3f ms  (%.1fx)\n", count,
                            instanced, looped, looped / (instanced > 0.0 ? instanced : 1.0));
            }
        }

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit InstancingLodExample(bool benchmark) : benchmark_(benchmark)
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
        bool benchmark = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;

        InstancingLodExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        // The registered ctest points at CNA_TEST_DISPLAY, which is not always a display that
        // exists; SKIP is the honest answer there, not a crash.
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
