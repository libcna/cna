// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredLightCompute.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "shaders/clustered_light_compute/ClusteredLightComputeShaderPackage.generated.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    namespace {

        constexpr int kGroupSize = 64;

        struct ClusteredLightParameters
        {
            std::array<std::int32_t, 4> Grid{};
            std::array<std::int32_t, 4> Output{};
            std::array<float, 4> Depth{};
        };
        static_assert(sizeof(ClusteredLightParameters) == 48);

        template <std::size_t N>
        [[nodiscard]] std::vector<std::uint8_t> ToBytes(const std::uint32_t (&words)[N])
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + sizeof(words));
        }

        [[nodiscard]] ShaderPackageEXT CreateAssignmentPackage()
        {
            using namespace detail::ClusteredLightComputeGenerated;
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "clustered_light_compute/assign.es.comp.glsl",
                                  std::string(kAssignEsComputeSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "clustered_light_compute/assign.desktop.comp.glsl",
                                  std::string(kAssignDesktopComputeSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "clustered_light_compute/assign.vulkan.comp.spv",
                                  ToBytes(kAssignVulkanComputeSpirV)),
                },
                {CNA::ShaderStageEXT::Compute},
                {
                    ShaderBindingRequirementEXT(
                        "CnaLights", 0, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Compute),
                    ShaderBindingRequirementEXT(
                        "CnaMatrix", 1, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Compute),
                    ShaderBindingRequirementEXT(
                        "CnaCounts", 2, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Compute),
                    ShaderBindingRequirementEXT(
                        "CnaIndices", 3, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Compute),
                    ShaderBindingRequirementEXT(
                        "ClusteredLightParameters", 4,
                        ShaderBindingTypeEXT::ConstantBuffer,
                        CNA::ShaderStageEXT::Compute),
                });
        }

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
            program_ = std::make_unique<ComputeShader>(device, CreateAssignmentPackage());
            parameters_ = std::make_unique<StorageBuffer>(
                device,
                StorageBufferDescriptor(
                    sizeof(ClusteredLightParameters), StorageBufferUsage::Constant,
                    StorageBufferCpuAccess::Write));
        }
        catch (const std::exception& error)
        {
            // A device that advertises compute and then refuses this program is a fallback, not a
            // failure: the CPU path answers the same question.
            program_.reset();
            parameters_.reset();
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
        const ClusteredLightParameters parameters{
            {grid.getTilesX(), grid.getTilesY(), grid.getSliceCount(), lightCount},
            {stride_, clusterCount, 0, 0},
            {grid.getNearPlane(), grid.getFarPlane(), 0.0f, 0.0f},
        };
        parameters_->setBytes(&parameters, sizeof(parameters));
        program_->bindConstantBuffer(4, *parameters_);
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
