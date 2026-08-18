// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ComputeShader.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    ComputeShader::ComputeShader(GraphicsDevice& device, const std::string& source)
        : device_(device)
    {
        if (!device_.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
            throw System::NotSupportedException(
                "CNA::Graphics::ComputeShader: the '"
                + std::string(device_.GetGraphicsRendererName())
                + "' renderer does not support compute shaders");

        renderer_ = device_.GetRenderer().CreateComputeShader(source);
        if (renderer_ == nullptr)
            throw System::NotSupportedException(
                "CNA::Graphics::ComputeShader: the '"
                + std::string(device_.GetGraphicsRendererName())
                + "' renderer reports compute support but produced no program");
        if (!renderer_->IsValid())
        {
            compileError_ = renderer_->GetCompileError();
            throw std::runtime_error("CNA::Graphics::ComputeShader: the program did not compile: "
                                     + compileError_);
        }
    }

    ComputeShader::~ComputeShader() = default;

    void ComputeShader::setUniform(const std::string& name, const int value)
    {
        renderer_->Bind();
        renderer_->SetUniformInt(name.c_str(), value);
    }

    void ComputeShader::setUniform(const std::string& name, const float value)
    {
        renderer_->Bind();
        renderer_->SetUniformFloat(name.c_str(), value);
    }

    void ComputeShader::bindStorageBuffer(const int binding, StorageBuffer& buffer)
    {
        if (binding < 0)
            throw std::invalid_argument(
                "CNA::Graphics::ComputeShader::bindStorageBuffer: the binding must not be negative");
        renderer_->Bind();
        renderer_->BindStorageBuffer(binding, buffer.getRendererEXT());
    }

    void ComputeShader::bindTexture(const int unit, const std::string& samplerName,
                                    Texture2D& texture)
    {
        if (unit < 0)
            throw std::invalid_argument(
                "CNA::Graphics::ComputeShader::bindTexture: the texture unit must not be negative");
        renderer_->Bind();
        renderer_->BindTexture(unit, &texture.GetRenderer());
        renderer_->SetUniformInt(samplerName.c_str(), unit);
    }

    bool ComputeShader::isImageBindingSupported() const
    {
        return device_.GetRenderer().SupportsComputeImageBindingEXT();
    }

    void ComputeShader::bindImage(const int unit, Texture2D& texture,
                                  const CNA::GraphicsImageAccess access)
    {
        if (unit < 0)
            throw std::invalid_argument(
                "CNA::Graphics::ComputeShader::bindImage: the image unit must not be negative");
        if (!isImageBindingSupported())
            throw System::NotSupportedException(
                "CNA::Graphics::ComputeShader::bindImage: the '"
                + std::string(device_.GetGraphicsRendererName())
                + "' renderer cannot bind a Texture2D as a compute image -- GL ES requires an "
                  "immutable texture and CNA allocates textures mutably. Write to a StorageBuffer "
                  "instead");
        renderer_->Bind();
        renderer_->BindImageTexture(unit, &texture.GetRenderer(), static_cast<int>(access));
    }

    void ComputeShader::dispatch(const int groupsX, const int groupsY, const int groupsZ)
    {
        if (groupsX <= 0 || groupsY <= 0 || groupsZ <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::ComputeShader::dispatch: every work-group count must be positive");

        const int requested[3] = {groupsX, groupsY, groupsZ};
        for (int axis = 0; axis < 3; ++axis)
        {
            const int limit = device_.GetMaxComputeWorkGroupCountEXT(axis);
            // A limit of 0 means the device declined to answer; refusing the dispatch on that
            // basis would be worse than letting the driver judge it.
            if (limit > 0 && requested[axis] > limit)
                throw std::invalid_argument(
                    "CNA::Graphics::ComputeShader::dispatch: axis "
                    + std::string(1, static_cast<char>('x' + axis)) + " asks for "
                    + std::to_string(requested[axis]) + " work groups but this device allows "
                    + std::to_string(limit));
        }

        renderer_->Bind();
        device_.GetRenderer().DispatchCompute(renderer_.get(), groupsX, groupsY, groupsZ);
        // MOD-1524: the two barriers a compute pass almost always needs, so a read-back or a
        // following dispatch sees the results without the caller having to know they are needed.
        // What the caller must still ask for is how the *rest of the pipeline* will read the data.
        barrier(CNA::GraphicsMemoryBarrier::ShaderStorage
                | CNA::GraphicsMemoryBarrier::ShaderImageAccess
                | CNA::GraphicsMemoryBarrier::BufferUpdate);
    }

    void ComputeShader::barrier(const CNA::GraphicsMemoryBarrier bits)
    {
        device_.GetRenderer().MemoryBarrierEXT(static_cast<int>(bits));
    }

    bool ComputeShader::isValid() const { return renderer_ != nullptr && renderer_->IsValid(); }

    const std::string& ComputeShader::getCompileError() const { return compileError_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
