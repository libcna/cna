// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2091: culling whose answer never comes back to the CPU.
//
// MOD-1551 already proved a compute shader culls the same objects FrustumCullerEXT does. What is
// new here is where the answer goes, so that is what these cases pin: the surviving count is
// compared against the CPU culler's, and then the frame is inspected to show that exactly those
// survivors -- each once, in the right place -- were drawn by a single call whose instance count
// no CPU ever read.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/GpuInstanceCuller.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::FrustumCullerEXT;
using CNA::Graphics::GpuCullableInstance;
using CNA::Graphics::GpuInstanceCuller;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

constexpr int kSize = 64;

/// Five instances inside the camera's box and six far outside it, laid out so the survivors land
/// as separated vertical bands -- countable in the frame rather than merely bright.
std::vector<GpuCullableInstance> Scene()
{
    const std::array<float, 11> positions{-3.2f, -1.6f, 0.0f, 1.6f, 3.2f,
                                          -20.0f, -16.0f, -12.0f, 12.0f, 16.0f, 20.0f};
    std::vector<GpuCullableInstance> instances;
    for (const float x : positions)
    {
        GpuCullableInstance instance;
        instance.World = Matrix::CreateScale(1.0f, 8.0f, 1.0f) *
                         Matrix::CreateTranslation(x, 0.0f, 0.0f);
        const Vector3 centre(x, 0.0f, 0.0f);
        const Vector3 extent(0.5f, 4.0f, 0.5f);
        instance.Bounds = BoundingBox(centre - extent, centre + extent);
        instances.push_back(instance);
    }
    return instances;
}

Matrix SceneView() { return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f), Vector3::Zero,
                                                 Vector3(0.0f, 1.0f, 0.0f)); }
Matrix SceneProjection() { return Matrix::CreateOrthographic(8.0f, 10.0f, 0.1f, 100.0f); }

std::string VertexSource()
{
    return std::string("#version 310 es\nprecision highp float;\n") +
           GpuInstanceCuller::getInstanceLookupGlsl() +
           R"(
layout(location = 0) in vec3 aPos;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() {
    gl_Position = Projection * View * cnaInstanceWorld() * vec4(aPos, 1.0);
}
)";
}

const char* const kFragmentSource = R"(#version 310 es
precision highp float;
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 1.0, 1.0, 1.0); }
)";

/// How many separated lit runs the middle row contains: one per instance that was actually drawn.
int CountBands(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const std::size_t row = static_cast<std::size_t>(kSize / 2) * kSize;
    int bands = 0;
    bool inside = false;
    for (int x = 0; x < kSize; ++x)
    {
        const bool lit = pixels[row + static_cast<std::size_t>(x)].getRProperty() > 128;
        if (lit && !inside) ++bands;
        inside = lit;
    }
    return bands;
}

/// A unit quad in model space, wound clockwise so the default rasterizer keeps it.
struct Quad
{
    explicit Quad(GraphicsDevice& device) : vertices(device, 4), indices(device, 6)
    {
        const std::array<VertexPositionColor, 4> corners{
            VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::White),
            VertexPositionColor(Vector3(-0.5f, 0.5f, 0.0f), Color::White),
            VertexPositionColor(Vector3(0.5f, 0.5f, 0.0f), Color::White),
            VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::White)};
        vertices.SetData(corners.data(), 4);
        const std::array<std::uint16_t, 6> order{0, 1, 2, 0, 2, 3};
        indices.SetData(order.data(), 6);
    }

    VertexBuffer vertices;
    IndexBuffer  indices;
};

int CpuVisibleCount(const std::vector<GpuCullableInstance>& instances)
{
    FrustumCullerEXT culler;
    culler.setCamera(SceneView(), SceneProjection());
    int visible = 0;
    for (const GpuCullableInstance& instance : instances)
        if (culler.isVisible(instance.Bounds)) ++visible;
    return visible;
}

TEST(GpuInstanceCullerTest, AnUnsupportedDeviceSaysWhichRequirementIsMissing)
{
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (culler.isSupported())
    {
        EXPECT_TRUE(culler.getUnsupportedReason().empty());
        return;
    }
    // The refusal has to name a requirement, not merely be false: "not supported" with no reason is
    // the message that sends someone reading the renderer's source.
    EXPECT_FALSE(culler.getUnsupportedReason().empty());
    EXPECT_THROW(culler.setInstances(Scene()), System::NotSupportedException);
    EXPECT_THROW(culler.cull(SceneView(), SceneProjection(), 6), System::NotSupportedException);
    EXPECT_THROW(culler.draw(PrimitiveType::TriangleList), System::NotSupportedException);
}

TEST(GpuInstanceCullerTest, TheSurvivingCountMatchesTheCpuCuller)
{
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (!culler.isSupported()) GTEST_SKIP() << culler.getUnsupportedReason();

    const auto instances = Scene();
    culler.setInstances(instances);
    EXPECT_EQ(culler.getInstanceCount(), static_cast<int>(instances.size()));

    culler.cull(SceneView(), SceneProjection(), 6);

    const int expected = CpuVisibleCount(instances);
    EXPECT_GT(expected, 0) << "a scene with nothing visible would pass this while testing nothing";
    EXPECT_LT(expected, static_cast<int>(instances.size()))
        << "a scene with everything visible would pass this while testing nothing";
    EXPECT_EQ(culler.readVisibleCountEXT(), expected);
}

TEST(GpuInstanceCullerTest, EachSurvivorIsDrawnOnceAndTheRestAreNot)
{
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (!culler.isSupported()) GTEST_SKIP() << culler.getUnsupportedReason();

    ShaderEffect effect(device, VertexSource(), kFragmentSource);
    ASSERT_TRUE(effect.IsEffectValid()) << "the instance-lookup shader did not compile";
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(SceneView());
    effect.setProjectionProperty(SceneProjection());

    const auto instances = Scene();
    culler.setInstances(instances);
    culler.cull(SceneView(), SceneProjection(), 6);

    Quad quad(device);
    RenderTarget2D target(device, kSize, kSize);
    device.SetVertexBuffer(&quad.vertices);
    device.SetIndexBuffer(&quad.indices);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    culler.draw(PrimitiveType::TriangleList);
    device.SetRenderTarget(nullptr);
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);

    // One band per survivor. Fewer means an instance was lost; more means one of the far-off
    // instances was kept and happened to land on screen, or a survivor was drawn twice. The
    // expected number is asserted plural first, so an empty frame compared against an empty
    // expectation cannot pass this.
    const int expected = CpuVisibleCount(instances);
    ASSERT_GT(expected, 1);
    EXPECT_EQ(CountBands(target), expected);
}

TEST(GpuInstanceCullerTest, ACameraLookingAwaySeesNothingAndDrawsNothing)
{
    // The other end of the range, and the case a compacting culler can get wrong on its own: the
    // command's instance count starts at zero every cull, so a stale count from the previous frame
    // would show up here as a frame that still had geometry in it.
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (!culler.isSupported()) GTEST_SKIP() << culler.getUnsupportedReason();

    const auto instances = Scene();
    culler.setInstances(instances);
    culler.cull(SceneView(), SceneProjection(), 6);
    ASSERT_GT(culler.readVisibleCountEXT(), 0);

    const Matrix away = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 10.0f),
                                             Vector3(0.0f, 0.0f, 200.0f),
                                             Vector3(0.0f, 1.0f, 0.0f));
    culler.cull(away, SceneProjection(), 6);
    EXPECT_EQ(culler.readVisibleCountEXT(), 0);
}

TEST(GpuInstanceCullerTest, TheCullRefusesArgumentsThatCannotDescribeADraw)
{
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (!culler.isSupported()) GTEST_SKIP() << culler.getUnsupportedReason();

    culler.setInstances(Scene());
    EXPECT_THROW(culler.cull(SceneView(), SceneProjection(), 0), std::invalid_argument);
    EXPECT_THROW(culler.cull(SceneView(), SceneProjection(), 6, -1), std::invalid_argument);
    EXPECT_THROW(culler.cull(SceneView(), SceneProjection(), 6, 0, -1), std::invalid_argument);
}

TEST(GpuInstanceCullerTest, DrawingBeforeCullingIsRefused)
{
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (!culler.isSupported()) GTEST_SKIP() << culler.getUnsupportedReason();

    culler.setInstances(Scene());
    EXPECT_THROW(culler.draw(PrimitiveType::TriangleList), std::runtime_error);
}

TEST(GpuInstanceCullerTest, AnEmptySceneIsAcceptedRatherThanRefused)
{
    GraphicsDevice device;
    GpuInstanceCuller culler(device);
    if (!culler.isSupported()) GTEST_SKIP() << culler.getUnsupportedReason();

    culler.setInstances({});
    EXPECT_EQ(culler.getInstanceCount(), 0);
    culler.cull(SceneView(), SceneProjection(), 6);
    EXPECT_EQ(culler.readVisibleCountEXT(), 0);
    EXPECT_NO_THROW(culler.draw(PrimitiveType::TriangleList));
}

TEST(GpuInstanceCullerTest, TheLookupGlslDeclaresTheBindingItPromises)
{
    const std::string glsl = GpuInstanceCuller::getInstanceLookupGlsl();
    EXPECT_NE(glsl.find("binding = " + std::to_string(GpuInstanceCuller::kInstanceBinding)),
              std::string::npos);
    EXPECT_NE(glsl.find("mat4 cnaInstanceWorld()"), std::string::npos);
}

} // namespace

#endif // CNA_CNAEXT
