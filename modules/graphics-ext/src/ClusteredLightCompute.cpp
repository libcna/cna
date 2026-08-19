// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredLightCompute.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    namespace {

        constexpr int kGroupSize = 64;

        // One invocation per cluster, walking every light in order. The naive shape on purpose:
        // with no atomics and no two invocations writing to the same place, a cluster's list comes
        // out sorted and identical to the one the CPU builds, which is the only way the two paths
        // can be compared for equality rather than for plausibility.
        constexpr const char* kComputeSource = R"(#version 310 es
layout(local_size_x = 64) in;

layout(std430, binding = 0) readonly buffer CnaLights   { vec4  uLights[]; };
layout(std430, binding = 1) readonly buffer CnaMatrix   { float uInverseProjection[]; };
layout(std430, binding = 2) buffer CnaCounts            { int   uCounts[]; };
layout(std430, binding = 3) buffer CnaIndices           { int   uIndices[]; };

uniform int   uTilesX;
uniform int   uTilesY;
uniform int   uSliceCount;
uniform int   uLightCount;
uniform int   uStride;
uniform int   uClusterCount;
uniform float uNearPlane;
uniform float uFarPlane;

vec3 cnaUnproject(mat4 inverseProjection, float x, float y, float z) {
    vec4 p = inverseProjection * vec4(x, y, z, 1.0);
    if (abs(p.w) <= 1e-9) return p.xyz;
    return p.xyz / p.w;
}

vec3 cnaAtDistance(vec3 atNear, vec3 atFar, float distance) {
    float span = atNear.z - atFar.z;
    if (abs(span) <= 1e-9) return atNear;
    float t = (atNear.z + distance) / span;
    return vec3(atNear.x + (atFar.x - atNear.x) * t,
                atNear.y + (atFar.y - atNear.y) * t,
                -distance);
}

float cnaSliceDistance(int slice) {
    if (slice == 0) return uNearPlane;
    if (slice == uSliceCount) return uFarPlane;
    return uNearPlane * pow(uFarPlane / uNearPlane, float(slice) / float(uSliceCount));
}

void main() {
    int cluster = int(gl_GlobalInvocationID.x);
    if (cluster >= uClusterCount) return;

    int x = cluster % uTilesX;
    int y = (cluster / uTilesX) % uTilesY;
    int slice = cluster / (uTilesX * uTilesY);

    mat4 inverseProjection = mat4(
        uInverseProjection[0],  uInverseProjection[1],  uInverseProjection[2],  uInverseProjection[3],
        uInverseProjection[4],  uInverseProjection[5],  uInverseProjection[6],  uInverseProjection[7],
        uInverseProjection[8],  uInverseProjection[9],  uInverseProjection[10], uInverseProjection[11],
        uInverseProjection[12], uInverseProjection[13], uInverseProjection[14], uInverseProjection[15]);

    float u0 = 2.0 * float(x)     / float(uTilesX) - 1.0;
    float u1 = 2.0 * float(x + 1) / float(uTilesX) - 1.0;
    float v0 = 2.0 * float(y)     / float(uTilesY) - 1.0;
    float v1 = 2.0 * float(y + 1) / float(uTilesY) - 1.0;
    float d0 = cnaSliceDistance(slice);
    float d1 = cnaSliceDistance(slice + 1);

    vec3 minimum = vec3(3.4028235e38);
    vec3 maximum = vec3(-3.4028235e38);
    for (int i = 0; i < 2; ++i) {
        float u = (i == 0) ? u0 : u1;
        for (int j = 0; j < 2; ++j) {
            float v = (j == 0) ? v0 : v1;
            vec3 atNear = cnaUnproject(inverseProjection, u, v, 0.0);
            vec3 atFar  = cnaUnproject(inverseProjection, u, v, 1.0);
            for (int k = 0; k < 2; ++k) {
                vec3 p = cnaAtDistance(atNear, atFar, (k == 0) ? d0 : d1);
                minimum = min(minimum, p);
                maximum = max(maximum, p);
            }
        }
    }

    int found = 0;
    for (int light = 0; light < uLightCount; ++light) {
        vec4 sphere = uLights[light];
        if (sphere.w <= 0.0) continue;
        vec3 nearest = clamp(sphere.xyz, minimum, maximum);
        vec3 delta = sphere.xyz - nearest;
        if (dot(delta, delta) > sphere.w * sphere.w) continue;
        if (found < uStride) uIndices[cluster * uStride + found] = light;
        ++found;
    }

    uCounts[cluster] = min(found, uStride);
    if (found > uStride) uCounts[uClusterCount] = 1;
}
)";

    } // namespace

    ClusteredLightCompute::ClusteredLightCompute(GraphicsDevice& device, const int stride)
        : device_(device), stride_(stride)
    {
        if (stride <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightCompute: the per-cluster capacity must be positive");

        if (!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
        {
            unsupportedReason_ = "this renderer has no compute shaders";
            return;
        }
        try
        {
            program_ = std::make_unique<ComputeShader>(device, kComputeSource);
        }
        catch (const std::exception& error)
        {
            // A device that advertises compute and then refuses this program is a fallback, not a
            // failure: the CPU path answers the same question.
            program_.reset();
            unsupportedReason_ = error.what();
        }
    }

    ClusteredLightCompute::~ClusteredLightCompute() = default;

    bool ClusteredLightCompute::isSupported() const { return program_ != nullptr; }
    const std::string& ClusteredLightCompute::getUnsupportedReason() const
    {
        return unsupportedReason_;
    }
    int  ClusteredLightCompute::getStride()    const { return stride_; }
    bool ClusteredLightCompute::usedCompute()  const { return usedCompute_; }
    bool ClusteredLightCompute::hasOverflowed() const { return overflowed_; }

    void ClusteredLightCompute::assign(const ClusteredLightGrid& grid, const Matrix& view,
                                       const std::vector<BoundingSphere>& lights,
                                       ClusteredLightAssignment& result)
    {
        usedCompute_ = false;
        overflowed_  = false;

        if (program_ == nullptr)
        {
            result.assign(grid, view, lights);
            return;
        }
        if (static_cast<int>(lights.size()) > ClusteredLightAssignment::kMaxLights)
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightCompute::assign: more lights than the assignment "
                "accepts");
        if (!grid.hasProjection())
            throw std::runtime_error(
                "CNA::Graphics::ClusteredLightCompute::assign: the grid has no projection");

        const int clusterCount = grid.getClusterCount();
        const int lightCount   = static_cast<int>(lights.size());

        // The spheres reach the shader already in view space, because a view matrix is the one
        // thing the shader would need that has nothing to do with the grid's shape.
        std::vector<float> viewSpheres(static_cast<std::size_t>(std::max(lightCount, 1)) * 4, 0.0f);
        for (int i = 0; i < lightCount; ++i)
        {
            const BoundingSphere& sphere = lights[static_cast<std::size_t>(i)];
            const Vector3 centre = Vector3::Transform(sphere.Center, view);
            viewSpheres[static_cast<std::size_t>(i) * 4 + 0] = centre.X;
            viewSpheres[static_cast<std::size_t>(i) * 4 + 1] = centre.Y;
            viewSpheres[static_cast<std::size_t>(i) * 4 + 2] = centre.Z;
            viewSpheres[static_cast<std::size_t>(i) * 4 + 3] = sphere.Radius;
        }

        std::vector<float> matrix(16, 0.0f);
        {
            // The grid's own inverse, not a fresh inversion of the same projection: two
            // inversions of one matrix differ in the last bits, and that is enough to move a light
            // that sits on a cluster boundary -- which would make "the two paths agree exactly"
            // false for a reason that has nothing to do with either path.
            const Matrix inverse = grid.getInverseProjection();
            const float* source = &inverse.M11;
            for (int i = 0; i < 16; ++i) matrix[static_cast<std::size_t>(i)] = source[i];
        }

        StorageBufferT<float> lightBuffer(device_, viewSpheres.size());
        lightBuffer.setData(viewSpheres);
        StorageBufferT<float> matrixBuffer(device_, matrix.size());
        matrixBuffer.setData(matrix);
        StorageBufferT<int> countBuffer(device_, static_cast<std::size_t>(clusterCount) + 1);
        countBuffer.setData(std::vector<int>(static_cast<std::size_t>(clusterCount) + 1, 0));
        StorageBufferT<int> indexBuffer(
            device_, static_cast<std::size_t>(clusterCount) * static_cast<std::size_t>(stride_));

        program_->bindStorageBuffer(0, lightBuffer.getBuffer());
        program_->bindStorageBuffer(1, matrixBuffer.getBuffer());
        program_->bindStorageBuffer(2, countBuffer.getBuffer());
        program_->bindStorageBuffer(3, indexBuffer.getBuffer());
        program_->setUniform("uTilesX", grid.getTilesX());
        program_->setUniform("uTilesY", grid.getTilesY());
        program_->setUniform("uSliceCount", grid.getSliceCount());
        program_->setUniform("uLightCount", lightCount);
        program_->setUniform("uStride", stride_);
        program_->setUniform("uClusterCount", clusterCount);
        program_->setUniform("uNearPlane", grid.getNearPlane());
        program_->setUniform("uFarPlane", grid.getFarPlane());
        program_->dispatch((clusterCount + kGroupSize - 1) / kGroupSize, 1, 1);

        const std::vector<int> counts = countBuffer.getData();
        const std::vector<int> packed = indexBuffer.getData();
        overflowed_ = counts[static_cast<std::size_t>(clusterCount)] != 0;

        std::vector<int> offsets(static_cast<std::size_t>(clusterCount) + 1, 0);
        std::vector<int> indices;
        for (int cluster = 0; cluster < clusterCount; ++cluster)
        {
            const int count = std::clamp(counts[static_cast<std::size_t>(cluster)], 0, stride_);
            for (int i = 0; i < count; ++i)
                indices.push_back(packed[static_cast<std::size_t>(cluster) * stride_ + i]);
            offsets[static_cast<std::size_t>(cluster) + 1] = static_cast<int>(indices.size());
        }

        result.adopt(lightCount, std::move(offsets), std::move(indices));
        usedCompute_ = true;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
