// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/InstancedRendererEXT.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdint>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::IEffectMatrices;
    using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
    using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    const VertexDeclaration& InstancedRendererEXT::getInstanceDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 1),
            VertexElement(16, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 2),
            VertexElement(32, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 3),
            VertexElement(48, VertexElementFormat::Vector4,
                          VertexElementUsage::TextureCoordinate, 4)};
        return declaration;
    }

    const VertexDeclaration& InstancedRendererEXT::getTintDeclaration()
    {
        static const VertexDeclaration declaration{
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 1)};
        return declaration;
    }

    InstancedRendererEXT::InstancedRendererEXT(GraphicsDevice& device, ModelMeshPart* part)
        : device_(device), part_(part)
    {
        if (part_ == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::InstancedRendererEXT: the mesh part must not be null");
        if (part_->getVertexBufferProperty() == nullptr ||
            part_->getIndexBufferProperty() == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::InstancedRendererEXT: the mesh part must have a vertex buffer and "
                "an index buffer");
        if (part_->getPrimitiveCountProperty() <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::InstancedRendererEXT: the mesh part draws no primitives");
    }

    InstancedRendererEXT::~InstancedRendererEXT() = default;

    void InstancedRendererEXT::setInstances(const std::vector<Matrix>& transforms)
    {
        transforms_ = transforms;
        instanceCount_ = static_cast<int>(transforms.size());
        if (instanceCount_ == 0) return;

        if (instanceBuffer_ == nullptr || instanceCount_ > instanceCapacity_)
        {
            instanceBuffer_ = std::make_unique<DynamicVertexBuffer>(
                device_, getInstanceDeclaration(), instanceCount_, BufferUsage::WriteOnly);
            instanceCapacity_ = instanceCount_;
        }
        // SetDataRaw rather than a SetDataOptions overload: CNA has no raw upload that takes one,
        // and the streaming hint a dynamic buffer needs is already carried by its type. What the
        // row asked for -- no reallocation on re-upload -- is the capacity check above.
        instanceBuffer_->SetDataRaw(transforms.data(), instanceCount_,
                                    static_cast<int>(sizeof(Matrix)));
        uploadTints();
    }

    void InstancedRendererEXT::setInstanceTints(const std::vector<Color>& tints)
    {
        tints_ = tints;
        uploadTints();
    }

    void InstancedRendererEXT::uploadTints()
    {
        if (!tintsEnabled_ || instanceCount_ == 0) return;

        // Packed to four bytes rather than uploaded as Color objects: the declaration's stride is
        // 4, and Microsoft::Xna::Framework::Color is a polymorphic type whose C++ object is wider.
        std::vector<std::uint32_t> padded;
        padded.reserve(static_cast<std::size_t>(instanceCount_));
        for (int i = 0; i < instanceCount_; ++i)
            padded.push_back(i < static_cast<int>(tints_.size())
                                 ? tints_[static_cast<std::size_t>(i)].getPackedValueProperty()
                                 : Color::White.getPackedValueProperty());
        if (tintBuffer_ == nullptr || instanceCount_ > tintCapacity_)
        {
            tintBuffer_ = std::make_unique<DynamicVertexBuffer>(
                device_, getTintDeclaration(), instanceCount_, BufferUsage::WriteOnly);
            tintCapacity_ = instanceCount_;
        }
        tintBuffer_->SetDataRaw(padded.data(), instanceCount_,
                                static_cast<int>(sizeof(std::uint32_t)));
    }

    void InstancedRendererEXT::setTintsEnabled(const bool enabled)
    {
        if (enabled == tintsEnabled_) return;
        tintsEnabled_ = enabled;
        uploadTints();
    }

    bool InstancedRendererEXT::isTintsEnabled() const { return tintsEnabled_; }

    bool InstancedRendererEXT::isInstancingSupported() const
    {
        // MOD-1621: the instanced path binds the transforms as stream 1, so it needs multi-stream
        // input as much as it needs instancing. Asking only for Instancing believed SDL_GPU, whose
        // Instancing answer is the base class's `true` default while its DrawInstancedPrimitives
        // is the base class's refusal.
        return device_.SupportsCapability(CNA::GraphicsCapability::Instancing) &&
               device_.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput);
    }

    void InstancedRendererEXT::setFallbackEnabled(const bool enabled)
    {
        fallbackEnabled_ = enabled;
    }

    bool InstancedRendererEXT::isFallbackEnabled() const { return fallbackEnabled_; }

    int InstancedRendererEXT::getInstanceCount() const { return instanceCount_; }

    int InstancedRendererEXT::getInstanceCapacity() const { return instanceCapacity_; }

    int InstancedRendererEXT::getLastDrawCallCount() const { return lastDrawCallCount_; }

    bool InstancedRendererEXT::didLastDrawInstance() const { return lastDrawInstanced_; }

    void InstancedRendererEXT::draw(Effect& effect)
    {
        lastDrawCallCount_ = 0;
        lastDrawInstanced_ = false;
        if (instanceCount_ == 0 || instanceBuffer_ == nullptr) return;

        const int primitiveCount = part_->getPrimitiveCountProperty();
        const int numVertices    = part_->getNumVerticesProperty();
        const int startIndex     = part_->getStartIndexProperty();
        const int vertexOffset   = part_->getVertexOffsetProperty();

        device_.setIndicesProperty(part_->getIndexBufferProperty());

        if (isInstancingSupported())
        {
            std::vector<VertexBufferBinding> bindings;
            bindings.emplace_back(part_->getVertexBufferProperty(), 0, 0);
            bindings.emplace_back(instanceBuffer_.get(), 0, 1);
            if (tintsEnabled_ && tintBuffer_ != nullptr)
                bindings.emplace_back(tintBuffer_.get(), 0, 1);
            device_.SetVertexBuffers(bindings);

            effect.Apply();
            device_.DrawInstancedPrimitives(part_->getPrimitiveTypeEXTProperty(), vertexOffset, 0,
                                            numVertices, startIndex, primitiveCount,
                                            instanceCount_);
            lastDrawCallCount_ = 1;
            lastDrawInstanced_ = true;
            return;
        }

        if (!fallbackEnabled_)
            throw std::logic_error(
                "CNA::Graphics::InstancedRendererEXT::draw: this renderer does not support "
                "instancing and the per-instance fallback is not enabled");

        auto* matrices = dynamic_cast<IEffectMatrices*>(&effect);
        if (matrices == nullptr)
            throw std::logic_error(
                "CNA::Graphics::InstancedRendererEXT::draw: the per-instance fallback needs an "
                "effect implementing IEffectMatrices, because a per-instance transform has "
                "nowhere else to go");

        const Matrix world = matrices->getWorldProperty();
        device_.SetVertexBuffer(part_->getVertexBufferProperty());
        for (const Matrix& instance : transforms_)
        {
            matrices->setWorldProperty(instance * world);
            effect.Apply();
            device_.DrawIndexedPrimitives(part_->getPrimitiveTypeEXTProperty(), vertexOffset, 0,
                                          numVertices, startIndex, primitiveCount);
            ++lastDrawCallCount_;
        }
        matrices->setWorldProperty(world);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
