// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/GpuInstanceCuller.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsMemoryBarrier.hpp"
#include "CNA/IndirectDrawArguments.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Plane;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;

    namespace {

        /// One instance as the shader sees it: the transform the survivor draws with, and the
        /// bounds the cull tests. Padded to std430's vec4 alignment.
        struct GpuInstanceRecord
        {
            float world[16];
            float centre[4];
            float extent[4];
        };
        static_assert(sizeof(GpuInstanceRecord) == 96, "std430 requires this exact record");

        constexpr int kInstancesBinding = 0;
        constexpr int kVisibleBinding   = 1;
        constexpr int kPlanesBinding    = 2;
        constexpr int kCommandBinding   = 3;

        /// The command is five words; the buffer is padded to eight so the atomic lands well clear
        /// of anything a driver might place after it.
        constexpr std::size_t kCommandBytes = 32;

        const char* const kCullSource = R"(#version 310 es
layout(local_size_x = 64) in;
struct CnaInstance { mat4 world; vec4 centre; vec4 extent; };
layout(std430, binding = 0) readonly  buffer CnaInstances { CnaInstance cnaInstances[]; };
layout(std430, binding = 1) writeonly buffer CnaVisible   { mat4 cnaVisible[]; };
layout(std430, binding = 2) readonly  buffer CnaPlanes    { vec4 cnaPlanes[]; };
layout(std430, binding = 3)           buffer CnaCommand   { uint cnaCommand[]; };
uniform int uCount;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= uint(uCount)) return;

    vec3 centre = cnaInstances[index].centre.xyz;
    vec3 extent = cnaInstances[index].extent.xyz;
    for (int i = 0; i < 6; ++i) {
        vec4 plane = cnaPlanes[i];
        // XNA's frustum plane normals point OUTWARD, so the test is the mirror of the textbook
        // one: the box is culled when even its nearest corner is still in front of the plane.
        float radius = dot(extent, abs(plane.xyz));
        if (dot(plane.xyz, centre) + plane.w - radius > 0.0) return;
    }

    // The survivors compact themselves. cnaCommand[1] is the indirect command's InstanceCount, so
    // the same atomic that reserves this instance's slot IS what tells the draw how much to draw --
    // there is no second pass and no count to carry anywhere.
    uint slot = atomicAdd(cnaCommand[1], 1u);
    cnaVisible[slot] = cnaInstances[index].world;
}
)";

    } // namespace

    GpuInstanceCuller::GpuInstanceCuller(GraphicsDevice& device)
        : device_(device)
    {
        if (!device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
            unsupportedReason_ = "this renderer has no compute shaders";
        else if (!device.SupportsCapability(CNA::GraphicsCapability::IndirectDraw))
            unsupportedReason_ = "this renderer has no indirect draw";
        else if (device.GetRenderer().GetMaxVertexShaderStorageBlocksEXT() < 1)
            unsupportedReason_ =
                "this context allows no storage buffer in a vertex shader, so a compacted instance "
                "list cannot be read by the draw";
        else if (!device.ExecutesShaderEffectSourceEXT())
            unsupportedReason_ =
                "this renderer accepts effect source without running it, so the instance lookup "
                "would never execute";

        if (!unsupportedReason_.empty()) return;

        program_ = std::make_unique<ComputeShader>(device, kCullSource);
        if (!program_->isValid())
        {
            unsupportedReason_ = "the culling program did not compile on this device";
            program_.reset();
            return;
        }

        planes_  = std::make_unique<StorageBuffer>(device, 6 * 4 * sizeof(float));
        command_ = std::make_unique<StorageBuffer>(device, kCommandBytes);
    }

    GpuInstanceCuller::~GpuInstanceCuller() = default;

    bool GpuInstanceCuller::isSupported() const { return unsupportedReason_.empty(); }

    const std::string& GpuInstanceCuller::getUnsupportedReason() const
    {
        return unsupportedReason_;
    }

    int GpuInstanceCuller::getInstanceCount() const { return instanceCount_; }

    void GpuInstanceCuller::setInstances(const std::vector<GpuCullableInstance>& instances)
    {
        if (!isSupported())
            throw System::NotSupportedException(
                "CNA::Graphics::GpuInstanceCuller::setInstances: " + unsupportedReason_);

        instanceCount_ = static_cast<int>(instances.size());
        culled_ = false;
        if (instanceCount_ == 0) return;

        if (instanceCount_ > capacity_)
        {
            capacity_ = instanceCount_;
            instances_ = std::make_unique<StorageBuffer>(
                device_, static_cast<std::size_t>(capacity_) * sizeof(GpuInstanceRecord));
            visible_ = std::make_unique<StorageBuffer>(
                device_, static_cast<std::size_t>(capacity_) * 16 * sizeof(float));
        }

        std::vector<GpuInstanceRecord> records(static_cast<std::size_t>(instanceCount_));
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            const GpuCullableInstance& instance = instances[i];
            // Column-major, which is the same form a custom effect's own `World` uniform arrives
            // in -- so `cnaInstanceWorld()` multiplies exactly like `World` does and a shader can
            // swap one for the other without touching anything else.
            instance.World.ToColumnMajor(records[i].world);

            const Vector3 centre = (instance.Bounds.Min + instance.Bounds.Max) * 0.5f;
            const Vector3 extent = (instance.Bounds.Max - instance.Bounds.Min) * 0.5f;
            records[i].centre[0] = centre.X; records[i].centre[1] = centre.Y;
            records[i].centre[2] = centre.Z; records[i].centre[3] = 0.0f;
            records[i].extent[0] = extent.X; records[i].extent[1] = extent.Y;
            records[i].extent[2] = extent.Z; records[i].extent[3] = 0.0f;
        }
        instances_->setBytes(records.data(), records.size() * sizeof(GpuInstanceRecord));
    }

    void GpuInstanceCuller::cull(const Matrix& view, const Matrix& projection,
                                 const int indexCount, const int firstIndex, const int baseVertex)
    {
        if (!isSupported())
            throw System::NotSupportedException(
                "CNA::Graphics::GpuInstanceCuller::cull: " + unsupportedReason_);
        if (indexCount <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::GpuInstanceCuller::cull: indexCount must be positive");
        if (firstIndex < 0 || baseVertex < 0)
            throw std::invalid_argument(
                "CNA::Graphics::GpuInstanceCuller::cull: the offsets must not be negative");

        // The command is written from the CPU with a count of ZERO and the shader adds to it. That
        // is an upload, not a readback: nothing waits for the GPU here.
        std::array<std::uint32_t, 8> command{};
        command[0] = static_cast<std::uint32_t>(indexCount);
        command[1] = 0;
        command[2] = static_cast<std::uint32_t>(firstIndex);
        command[3] = static_cast<std::uint32_t>(baseVertex);
        command[4] = 0;
        command_->setBytes(command.data(), command.size() * sizeof(std::uint32_t));
        culled_ = true;
        if (instanceCount_ == 0) return;

        const BoundingFrustum frustum(view * projection);
        const std::array<Plane, 6> planes{frustum.getNearProperty(), frustum.getFarProperty(),
                                          frustum.getLeftProperty(), frustum.getRightProperty(),
                                          frustum.getTopProperty(),  frustum.getBottomProperty()};
        std::array<float, 24> planeData{};
        for (std::size_t i = 0; i < planes.size(); ++i)
        {
            planeData[i * 4 + 0] = planes[i].Normal.X;
            planeData[i * 4 + 1] = planes[i].Normal.Y;
            planeData[i * 4 + 2] = planes[i].Normal.Z;
            planeData[i * 4 + 3] = planes[i].D;
        }
        planes_->setBytes(planeData.data(), planeData.size() * sizeof(float));

        program_->bindStorageBuffer(kInstancesBinding, *instances_);
        program_->bindStorageBuffer(kVisibleBinding, *visible_);
        program_->bindStorageBuffer(kPlanesBinding, *planes_);
        program_->bindStorageBuffer(kCommandBinding, *command_);
        program_->setUniform("uCount", instanceCount_);
        program_->dispatch((instanceCount_ + 63) / 64);

        // Three accesses have to be ordered, and they are three separate bits for a reason: the
        // survivors will be read by a vertex shader, the count will be FETCHED as a command rather
        // than read as storage, and both live in buffers the dispatch has just written.
        program_->barrier(CNA::GraphicsMemoryBarrier::ShaderStorage |
                          CNA::GraphicsMemoryBarrier::IndirectCommand |
                          CNA::GraphicsMemoryBarrier::BufferUpdate);
    }

    void GpuInstanceCuller::draw(const PrimitiveType primitiveType)
    {
        if (!isSupported())
            throw System::NotSupportedException(
                "CNA::Graphics::GpuInstanceCuller::draw: " + unsupportedReason_);
        if (!culled_)
            throw std::runtime_error(
                "CNA::Graphics::GpuInstanceCuller::draw: nothing has been culled yet");
        if (instanceCount_ == 0) return;

        device_.GetRenderer().BindStorageBufferForDrawEXT(kInstanceBinding,
                                                          *visible_->getRendererEXT());
        device_.DrawIndexedPrimitivesIndirectEXT(primitiveType, *command_->getRendererEXT(), 0);
    }

    std::string GpuInstanceCuller::getInstanceLookupGlsl()
    {
        return R"(
layout(std430, binding = 6) readonly buffer CnaVisibleInstances { mat4 cnaVisibleInstances[]; };

/// The world matrix of the instance this vertex belongs to. The survivors were compacted, so
/// gl_InstanceID indexes the list of what SURVIVED and never the list that was offered.
mat4 cnaInstanceWorld() { return cnaVisibleInstances[gl_InstanceID]; }
)";
    }

    int GpuInstanceCuller::readVisibleCountEXT() const
    {
        if (!culled_ || command_ == nullptr) return 0;
        std::array<std::uint32_t, 8> command{};
        command_->getBytes(command.data(), command.size() * sizeof(std::uint32_t));
        return static_cast<int>(command[1]);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
