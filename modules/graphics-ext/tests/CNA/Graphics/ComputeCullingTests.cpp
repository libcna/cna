// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1551: frustum culling on the GPU, checked against the CPU implementation.
//
// The interesting property is not that a compute shader can cull -- it is that it culls *the same
// objects*. A GPU culler that disagreed with FrustumCullerEXT by one object at a plane boundary
// would show up as geometry that pops in and out at the edge of the screen, which is exactly the
// kind of bug nobody traces back to a shader. So this asserts equality against the CPU answer for
// a scene laid out to put many boxes near the planes, rather than asserting a count.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <array>
#include <vector>

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using CNA::Graphics::ComputeShader;
using CNA::Graphics::FrustumCullerEXT;
using CNA::Graphics::StorageBufferT;

namespace {

    /// One box as the shader sees it: centre and half-extent, padded to std430's vec4 alignment.
    struct GpuBox
    {
        float centre[4];
        float extent[4];
    };

    const char* const kCuller = R"(#version 310 es
layout(local_size_x = 64) in;
struct Box { vec4 centre; vec4 extent; };
layout(std430, binding = 0) readonly buffer Boxes { Box boxes[]; };
layout(std430, binding = 1) writeonly buffer Visible { int visible[]; };
layout(std430, binding = 2) readonly buffer Planes { vec4 planes[]; };
uniform int uCount;
void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(uCount)) return;
    vec3 centre = boxes[index].centre.xyz;
    vec3 extent = boxes[index].extent.xyz;
    int inside = 1;
    for (int i = 0; i < 6; ++i) {
        vec4 plane = planes[i];
        // XNA's frustum plane normals point OUTWARD -- BoundingFrustum::Contains treats a box
        // entirely on a plane's front side as disjoint -- so the test is the mirror of the usual
        // one: the box is culled when even its nearest corner is still in front of the plane.
        float radius = dot(extent, abs(plane.xyz));
        if (dot(plane.xyz, centre) + plane.w - radius > 0.0) inside = 0;
    }
    visible[index] = inside;
}
)";

} // namespace

TEST(ComputeCullingTest, TheGpuCullerAgreesWithTheCpuOneBoxForBox)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
        GTEST_SKIP() << "this renderer does not support compute shaders";

    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 10.0f, 30.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 1.0f, 120.0f);
    FrustumCullerEXT culler;
    culler.setCamera(view, projection);

    // A grid wide and deep enough that a large share of it lies across the frustum's sides, its
    // near plane and its far plane -- the boundaries where the two implementations could differ.
    std::vector<BoundingBox> bounds;
    std::vector<GpuBox> gpuBoxes;
    for (int z = -12; z <= 12; ++z)
        for (int x = -12; x <= 12; ++x)
        {
            const Vector3 centre(static_cast<float>(x) * 6.0f, 0.0f, static_cast<float>(z) * 6.0f);
            const Vector3 extent(1.5f, 1.5f, 1.5f);
            bounds.emplace_back(centre - extent, centre + extent);
            gpuBoxes.push_back(GpuBox{{centre.X, centre.Y, centre.Z, 0.0f},
                                      {extent.X, extent.Y, extent.Z, 0.0f}});
        }
    const int count = static_cast<int>(bounds.size());

    // The same six planes the CPU culler holds, in XNA's own normalized form.
    const auto& frustum = culler.getFrustum();
    const std::array<Plane, 6> planes{frustum.getNearProperty(),  frustum.getFarProperty(),
                                      frustum.getLeftProperty(),  frustum.getRightProperty(),
                                      frustum.getTopProperty(),   frustum.getBottomProperty()};
    std::vector<Vector4> gpuPlanes;
    gpuPlanes.reserve(6);
    for (const Plane& plane : planes)
        gpuPlanes.emplace_back(plane.Normal.X, plane.Normal.Y, plane.Normal.Z, plane.D);

    StorageBufferT<GpuBox> boxBuffer(gd, static_cast<std::size_t>(count));
    boxBuffer.setData(gpuBoxes);
    StorageBufferT<std::int32_t> visibleBuffer(gd, static_cast<std::size_t>(count));
    StorageBufferT<Vector4> planeBuffer(gd, 6);
    planeBuffer.setData(gpuPlanes);

    ComputeShader shader(gd, kCuller);
    shader.bindStorageBuffer(0, boxBuffer.getBuffer());
    shader.bindStorageBuffer(1, visibleBuffer.getBuffer());
    shader.bindStorageBuffer(2, planeBuffer.getBuffer());
    shader.setUniform("uCount", count);
    shader.dispatch((count + 63) / 64);

    const std::vector<std::int32_t> gpuVisible = visibleBuffer.getData();
    ASSERT_EQ(gpuVisible.size(), static_cast<std::size_t>(count));

    int visibleOnGpu = 0;
    int disagreements = 0;
    for (int i = 0; i < count; ++i)
    {
        const bool cpu = culler.isVisible(bounds[static_cast<std::size_t>(i)]);
        const bool gpu = gpuVisible[static_cast<std::size_t>(i)] != 0;
        if (gpu) ++visibleOnGpu;
        if (cpu != gpu)
        {
            ++disagreements;
            EXPECT_EQ(cpu, gpu) << "box " << i << " at ("
                                << gpuBoxes[static_cast<std::size_t>(i)].centre[0] << ", "
                                << gpuBoxes[static_cast<std::size_t>(i)].centre[2] << ")";
        }
    }
    EXPECT_EQ(disagreements, 0);
    // A scene where everything -- or nothing -- is visible would pass the comparison above while
    // testing nothing, so the split itself is asserted.
    EXPECT_GT(visibleOnGpu, 0);
    EXPECT_LT(visibleOnGpu, count);
}

#endif // CNA_CNAEXT
