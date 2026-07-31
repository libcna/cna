// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Llgl/LlglGraphicsBackend.hpp"

#include "CNA/Internal/Backends/Common/NotYetImplemented.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <LLGL/Utils/VertexFormat.h>

#include "shaders/llgl_shaders.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::Llgl
{
    namespace
    {
        constexpr const char* kBackendName = "LLGL";

        /// Floats in the 3D effect uniform block: see shaders/effect3d_common.glsl.inc.
        constexpr std::size_t kEffectUniformFloats = 32;

        /// Floats per sprite vertex: position (2), texture coordinate (2), colour (4).
        constexpr std::size_t kSpriteVertexFloats = 8;
        constexpr std::size_t kSpriteVertexStride = kSpriteVertexFloats * sizeof(float);
        constexpr std::size_t kSpriteVerticesPerQuad = 6;

        using XnaBlend = Microsoft::Xna::Framework::Graphics::Blend;
        using XnaBlendFunction = Microsoft::Xna::Framework::Graphics::BlendFunction;
        using XnaTextureFilter = Microsoft::Xna::Framework::Graphics::TextureFilter;
        using XnaTextureAddressMode = Microsoft::Xna::Framework::Graphics::TextureAddressMode;

        LLGL::BlendOp MapBlendFactor(int blend)
        {
            switch (static_cast<XnaBlend>(blend))
            {
                case XnaBlend::One:                       return LLGL::BlendOp::One;
                case XnaBlend::Zero:                      return LLGL::BlendOp::Zero;
                case XnaBlend::SourceColor:               return LLGL::BlendOp::SrcColor;
                case XnaBlend::InverseSourceColor:        return LLGL::BlendOp::InvSrcColor;
                case XnaBlend::SourceAlpha:               return LLGL::BlendOp::SrcAlpha;
                case XnaBlend::InverseSourceAlpha:        return LLGL::BlendOp::InvSrcAlpha;
                case XnaBlend::DestinationColor:          return LLGL::BlendOp::DstColor;
                case XnaBlend::InverseDestinationColor:   return LLGL::BlendOp::InvDstColor;
                case XnaBlend::DestinationAlpha:          return LLGL::BlendOp::DstAlpha;
                case XnaBlend::InverseDestinationAlpha:   return LLGL::BlendOp::InvDstAlpha;
                case XnaBlend::BlendFactor:               return LLGL::BlendOp::BlendFactor;
                case XnaBlend::InverseBlendFactor:        return LLGL::BlendOp::InvBlendFactor;
                case XnaBlend::SourceAlphaSaturation:     return LLGL::BlendOp::SrcAlphaSaturate;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown Blend ordinal " + std::to_string(blend));
        }

        LLGL::BlendArithmetic MapBlendFunction(int function)
        {
            switch (static_cast<XnaBlendFunction>(function))
            {
                case XnaBlendFunction::Add:             return LLGL::BlendArithmetic::Add;
                case XnaBlendFunction::Subtract:        return LLGL::BlendArithmetic::Subtract;
                case XnaBlendFunction::ReverseSubtract: return LLGL::BlendArithmetic::RevSubtract;
                case XnaBlendFunction::Max:             return LLGL::BlendArithmetic::Max;
                case XnaBlendFunction::Min:             return LLGL::BlendArithmetic::Min;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown BlendFunction ordinal " +
                std::to_string(function));
        }

        /// Complete min/mag/mip triple for every XNA TextureFilter, not just the two symmetric
        /// ones -- collapsing the six asymmetric entries onto Linear/Point is exactly the defect
        /// REMED-GFX-170 removed from the other backends.
        void MapTextureFilter(int filter, LLGL::SamplerFilter& minFilter, LLGL::SamplerFilter& magFilter,
                              LLGL::SamplerFilter& mipFilter, bool& anisotropic)
        {
            constexpr LLGL::SamplerFilter kLinear = LLGL::SamplerFilter::Linear;
            constexpr LLGL::SamplerFilter kPoint  = LLGL::SamplerFilter::Nearest;
            anisotropic = false;

            switch (static_cast<XnaTextureFilter>(filter))
            {
                case XnaTextureFilter::Linear:
                    minFilter = kLinear; magFilter = kLinear; mipFilter = kLinear; return;
                case XnaTextureFilter::Point:
                    minFilter = kPoint;  magFilter = kPoint;  mipFilter = kPoint;  return;
                case XnaTextureFilter::Anisotropic:
                    minFilter = kLinear; magFilter = kLinear; mipFilter = kLinear; anisotropic = true; return;
                case XnaTextureFilter::LinearMipPoint:
                    minFilter = kLinear; magFilter = kLinear; mipFilter = kPoint;  return;
                case XnaTextureFilter::PointMipLinear:
                    minFilter = kPoint;  magFilter = kPoint;  mipFilter = kLinear; return;
                case XnaTextureFilter::MinLinearMagPointMipLinear:
                    minFilter = kLinear; magFilter = kPoint;  mipFilter = kLinear; return;
                case XnaTextureFilter::MinLinearMagPointMipPoint:
                    minFilter = kLinear; magFilter = kPoint;  mipFilter = kPoint;  return;
                case XnaTextureFilter::MinPointMagLinearMipLinear:
                    minFilter = kPoint;  magFilter = kLinear; mipFilter = kLinear; return;
                case XnaTextureFilter::MinPointMagLinearMipPoint:
                    minFilter = kPoint;  magFilter = kLinear; mipFilter = kPoint;  return;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown TextureFilter ordinal " + std::to_string(filter));
        }

        LLGL::SamplerAddressMode MapAddressMode(int addressMode)
        {
            switch (static_cast<XnaTextureAddressMode>(addressMode))
            {
                case XnaTextureAddressMode::Wrap:   return LLGL::SamplerAddressMode::Repeat;
                case XnaTextureAddressMode::Clamp:  return LLGL::SamplerAddressMode::Clamp;
                case XnaTextureAddressMode::Mirror: return LLGL::SamplerAddressMode::Mirror;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown TextureAddressMode ordinal " +
                std::to_string(addressMode));
        }

        /// True when a blend factor ordinal refers to the constant blend colour, i.e. the only case
        /// in which the pipeline needs dynamic blend-factor state at all.
        bool UsesConstantBlendFactor(int blend)
        {
            const XnaBlend value = static_cast<XnaBlend>(blend);
            return value == XnaBlend::BlendFactor || value == XnaBlend::InverseBlendFactor;
        }

        using XnaCompareFunction = Microsoft::Xna::Framework::Graphics::CompareFunction;
        using XnaCullMode = Microsoft::Xna::Framework::Graphics::CullMode;
        using XnaFillMode = Microsoft::Xna::Framework::Graphics::FillMode;
        using XnaVertexElementFormat = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using XnaVertexElementUsage = Microsoft::Xna::Framework::Graphics::VertexElementUsage;

        LLGL::CompareOp MapCompareFunction(int compareFunction)
        {
            switch (static_cast<XnaCompareFunction>(compareFunction))
            {
                case XnaCompareFunction::Always:       return LLGL::CompareOp::AlwaysPass;
                case XnaCompareFunction::Never:        return LLGL::CompareOp::NeverPass;
                case XnaCompareFunction::Less:         return LLGL::CompareOp::Less;
                case XnaCompareFunction::LessEqual:    return LLGL::CompareOp::LessEqual;
                case XnaCompareFunction::Equal:        return LLGL::CompareOp::Equal;
                case XnaCompareFunction::GreaterEqual: return LLGL::CompareOp::GreaterEqual;
                case XnaCompareFunction::Greater:      return LLGL::CompareOp::Greater;
                case XnaCompareFunction::NotEqual:     return LLGL::CompareOp::NotEqual;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown CompareFunction ordinal " +
                std::to_string(compareFunction));
        }

        /// XNA names the winding it CULLS, and treats clockwise as front-facing (the Direct3D
        /// convention). The pipeline is therefore configured with front = clockwise, which is what
        /// makes "cull the counter-clockwise faces" the same thing as culling back faces.
        LLGL::CullMode MapCullMode(int cullMode)
        {
            switch (static_cast<XnaCullMode>(cullMode))
            {
                case XnaCullMode::None:                    return LLGL::CullMode::Disabled;
                case XnaCullMode::CullClockwiseFace:       return LLGL::CullMode::Front;
                case XnaCullMode::CullCounterClockwiseFace: return LLGL::CullMode::Back;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown CullMode ordinal " + std::to_string(cullMode));
        }

        LLGL::PolygonMode MapFillMode(int fillMode)
        {
            switch (static_cast<XnaFillMode>(fillMode))
            {
                case XnaFillMode::Solid:     return LLGL::PolygonMode::Fill;
                case XnaFillMode::WireFrame: return LLGL::PolygonMode::Wireframe;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown FillMode ordinal " + std::to_string(fillMode));
        }

        LLGL::PrimitiveTopology MapPrimitiveTopology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return LLGL::PrimitiveTopology::TriangleList;
                case PrimitiveType::TriangleStrip: return LLGL::PrimitiveTopology::TriangleStrip;
                case PrimitiveType::LineList:      return LLGL::PrimitiveTopology::LineList;
                case PrimitiveType::LineStrip:     return LLGL::PrimitiveTopology::LineStrip;
                case PrimitiveType::PointListEXT:  return LLGL::PrimitiveTopology::PointList;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown PrimitiveType ordinal " +
                std::to_string(static_cast<int>(primitive)));
        }

        /// Number of vertices (or indices) a draw of @p primitiveCount primitives consumes.
        int CountElementsForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown PrimitiveType ordinal " +
                std::to_string(static_cast<int>(primitive)));
        }

        LLGL::Format MapVertexElementFormat(XnaVertexElementFormat format)
        {
            switch (format)
            {
                case XnaVertexElementFormat::Single:  return LLGL::Format::R32Float;
                case XnaVertexElementFormat::Vector2: return LLGL::Format::RG32Float;
                case XnaVertexElementFormat::Vector3: return LLGL::Format::RGB32Float;
                case XnaVertexElementFormat::Vector4: return LLGL::Format::RGBA32Float;
                // XNA's packed Color is four normalized bytes. CNA's Color stores them R,G,B,A in
                // memory (its packed value is AABBGGRR little-endian), so this is RGBA8UNorm and
                // not the BGRA8 the XNA documentation's own naming might suggest.
                case XnaVertexElementFormat::Color:   return LLGL::Format::RGBA8UNorm;
                case XnaVertexElementFormat::Byte4:   return LLGL::Format::RGBA8UInt;
                case XnaVertexElementFormat::Short2:  return LLGL::Format::RG16SInt;
                case XnaVertexElementFormat::Short4:  return LLGL::Format::RGBA16SInt;
                case XnaVertexElementFormat::NormalizedShort2: return LLGL::Format::RG16SNorm;
                case XnaVertexElementFormat::NormalizedShort4: return LLGL::Format::RGBA16SNorm;
                case XnaVertexElementFormat::HalfVector2: return LLGL::Format::RG16Float;
                case XnaVertexElementFormat::HalfVector4: return LLGL::Format::RGBA16Float;
            }
            throw std::runtime_error(
                std::string(kBackendName) + " backend: unknown VertexElementFormat ordinal " +
                std::to_string(static_cast<int>(format)));
        }

        /// Shader-side name and attribute location for a vertex usage.
        ///
        /// Locations are assigned by USAGE, not by the order elements happen to appear in the
        /// declaration, so two declarations that carry the same semantics in a different order
        /// still feed the same shader inputs. Only the usages the 3D shaders actually declare are
        /// mapped; anything else is refused rather than silently bound to a location the shader
        /// reads as something different.
        bool MapVertexUsage(XnaVertexElementUsage usage, const char*& name, std::uint32_t& location)
        {
            switch (usage)
            {
                case XnaVertexElementUsage::Position:          name = "position"; location = 0; return true;
                case XnaVertexElementUsage::Color:             name = "color";    location = 1; return true;
                case XnaVertexElementUsage::TextureCoordinate: name = "texCoord"; location = 2; return true;
                case XnaVertexElementUsage::Normal:            name = "normal";   location = 3; return true;
                default: return false;
            }
        }

        std::uint64_t MakeVertexLayoutKey(const std::vector<LLGL::VertexAttribute>& attributes)
        {
            // FNV-1a over the fields that genuinely change the input layout. Two layouts that hash
            // equal really are the same layout; nothing else about the buffer takes part.
            std::uint64_t hash = 1469598103934665603ull;
            const auto mix = [&hash](std::uint64_t value) {
                hash ^= value;
                hash *= 1099511628211ull;
            };
            for (const LLGL::VertexAttribute& attribute : attributes)
            {
                mix(static_cast<std::uint64_t>(attribute.location));
                mix(static_cast<std::uint64_t>(attribute.format));
                mix(static_cast<std::uint64_t>(attribute.offset));
                mix(static_cast<std::uint64_t>(attribute.stride));
            }
            return hash;
        }

        std::uint8_t MapColorWriteMask(int colorWriteChannels)
        {
            std::uint8_t mask = 0;
            if (ColorWriteHasRed(colorWriteChannels))   mask |= LLGL::ColorMaskFlags::R;
            if (ColorWriteHasGreen(colorWriteChannels)) mask |= LLGL::ColorMaskFlags::G;
            if (ColorWriteHasBlue(colorWriteChannels))  mask |= LLGL::ColorMaskFlags::B;
            if (ColorWriteHasAlpha(colorWriteChannels)) mask |= LLGL::ColorMaskFlags::A;
            return mask;
        }

        LLGL::VertexFormat MakeSpriteVertexFormat()
        {
            LLGL::VertexFormat format;
            format.AppendAttribute({"position", LLGL::Format::RG32Float});
            format.AppendAttribute({"texCoord", LLGL::Format::RG32Float});
            format.AppendAttribute({"color", LLGL::Format::RGBA32Float});
            format.SetStride(static_cast<std::uint32_t>(kSpriteVertexStride));
            return format;
        }

        bool SupportsShadingLanguage(const LLGL::RenderingCapabilities& caps, LLGL::ShadingLanguage language)
        {
            return std::find(caps.shadingLanguages.begin(), caps.shadingLanguages.end(), language) !=
                   caps.shadingLanguages.end();
        }
    }

    // -----------------------------------------------------------------------------------------
    // LlglTextureBackend
    // -----------------------------------------------------------------------------------------

    LlglTextureBackend::LlglTextureBackend(LLGL::RenderSystem* renderSystem, LLGL::Texture* texture,
                                           int width, int height, int mipLevels)
        : renderSystem_(renderSystem)
        , texture_(texture)
        , width_(width)
        , height_(height)
        , mipLevels_(mipLevels > 0 ? mipLevels : 1)
    {
        if (renderSystem_ == nullptr || texture_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: texture creation failed");
    }

    LlglTextureBackend::~LlglTextureBackend()
    {
        if (renderSystem_ != nullptr && texture_ != nullptr)
            renderSystem_->Release(*texture_);
    }

    void LlglTextureBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (rgba == nullptr || width_ <= 0 || height_ <= 0)
            return;

        const int tightStride = width_ * 4;
        const int rowStride = stride > 0 ? stride : tightStride;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = 0;
        region.subresource.numMipLevels = 1;
        region.offset = {0, 0, 0};
        region.extent = {static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};

        LLGL::ImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = rgba;
        imageView.dataSize = static_cast<std::size_t>(rowStride) * static_cast<std::size_t>(height_);
        imageView.rowStride = static_cast<std::uint32_t>(rowStride);

        renderSystem_->WriteTexture(*texture_, region, imageView);
    }

    void LlglTextureBackend::UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH)
    {
        if (rgba == nullptr || level < 0 || level >= mipLevels_ || levelW <= 0 || levelH <= 0)
            return;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {0, 0, 0};
        region.extent = {static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH), 1};

        LLGL::ImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = rgba;
        imageView.dataSize = static_cast<std::size_t>(levelW) * static_cast<std::size_t>(levelH) * 4u;

        renderSystem_->WriteTexture(*texture_, region, imageView);
    }

    bool LlglTextureBackend::GetData(int level, int x, int y, int w, int h,
                                      void* data, int dataLength) const
    {
        if (data == nullptr || w <= 0 || h <= 0 || level < 0 || level >= mipLevels_)
            return false;

        const std::size_t required = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required)
            return false;

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = static_cast<std::uint32_t>(level);
        region.subresource.numMipLevels = 1;
        region.offset = {x, y, 0};
        region.extent = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1};

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = data;
        imageView.dataSize = required;

        renderSystem_->ReadTexture(*texture_, region, imageView);
        return true;
    }

    // -----------------------------------------------------------------------------------------
    // LlglVertexBufferBackend
    // -----------------------------------------------------------------------------------------

    LlglVertexBufferBackend::LlglVertexBufferBackend(LLGL::RenderSystem* renderSystem,
                                                      LlglGraphicsBackend* owner, int vertexCapacity)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , vertexCapacity_(vertexCapacity > 0 ? vertexCapacity : 0)
    {
        if (renderSystem_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: null render system for vertex buffer");
    }

    LlglVertexBufferBackend::~LlglVertexBufferBackend()
    {
        if (buffer_ == nullptr)
            return;

        // Handed to the backend rather than released here: a frame recorded earlier this tick may
        // still refer to this buffer, and it is only submitted at Present().
        if (owner_ != nullptr)
            owner_->ScheduleBufferReleaseEXT(buffer_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*buffer_);
    }

    void LlglVertexBufferBackend::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        if (data == nullptr || vertexCount <= 0 || strideInBytes == 0)
        {
            vertexCount_ = 0;
            return;
        }

        ResolveVertexAttributes(strideInBytes);

        const std::size_t byteSize = static_cast<std::size_t>(vertexCount) * strideInBytes;
        if (buffer_ == nullptr || byteSize > byteCapacity_)
        {
            if (buffer_ != nullptr)
                renderSystem_->Release(*buffer_);

            LLGL::BufferDescriptor bufferDesc;
            bufferDesc.size = byteSize;
            bufferDesc.stride = static_cast<std::uint32_t>(strideInBytes);
            bufferDesc.bindFlags = LLGL::BindFlags::VertexBuffer;
            // The attributes go on the buffer as well as on the shader: OpenGL builds this
            // buffer's vertex array from them, while Vulkan takes the layout from the shader. Both
            // come from the same translation, so the two cannot drift apart.
            bufferDesc.vertexAttribs = attributes_;
            buffer_ = renderSystem_->CreateBuffer(bufferDesc, data);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string(kBackendName) + " backend: vertex buffer creation failed");
            byteCapacity_ = byteSize;
        }
        else
        {
            renderSystem_->WriteBuffer(*buffer_, 0, data, byteSize);
        }

        vertexCount_ = vertexCount;
        stride_ = strideInBytes;
    }

    void LlglVertexBufferBackend::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // Stored, not translated yet: the translation needs the upload stride as a fallback for a
        // declaration that reports none, and SetData() has not run at this point.
        declaration_ = vertexDeclaration;
        hasDeclaration_ = true;
    }

    void LlglVertexBufferBackend::ResolveVertexAttributes(std::size_t strideInBytes)
    {
        attributes_.clear();

        const auto stride = static_cast<std::uint32_t>(
            declaration_.getVertexStrideProperty() > 0
                ? static_cast<std::uint32_t>(declaration_.getVertexStrideProperty())
                : static_cast<std::uint32_t>(strideInBytes));

        if (hasDeclaration_ && !declaration_.GetVertexElements().empty())
        {
            for (const VertexElement& element : declaration_.GetVertexElements())
            {
                const char* name = nullptr;
                std::uint32_t location = 0;
                if (!MapVertexUsage(element.getVertexElementUsageProperty(), name, location))
                {
                    // Deliberately skipped rather than assigned some spare location: a usage no
                    // shader in this backend declares has no correct location, and inventing one
                    // would feed the wrong bytes to whatever input happened to sit there.
                    continue;
                }

                LLGL::VertexAttribute attribute;
                attribute.name = name;
                attribute.format = MapVertexElementFormat(element.getVertexElementFormatProperty());
                attribute.location = location;
                attribute.offset = static_cast<std::uint32_t>(element.getOffsetProperty());
                attribute.stride = stride;
                attributes_.push_back(attribute);
            }
            return;
        }

        // No declaration: recognise the one layout the colour-only 3D path is defined for
        // (VertexPositionColor -- Vector3 position then a packed Color, 16 bytes). Anything else
        // is left empty, and the draw path refuses it by name instead of guessing.
        if (strideInBytes == 16)
        {
            LLGL::VertexAttribute position;
            position.name = "position";
            position.format = LLGL::Format::RGB32Float;
            position.location = 0;
            position.offset = 0;
            position.stride = stride;

            LLGL::VertexAttribute color;
            color.name = "color";
            color.format = LLGL::Format::RGBA8UNorm;
            color.location = 1;
            color.offset = 12;
            color.stride = stride;

            attributes_ = {position, color};
        }
    }

    // -----------------------------------------------------------------------------------------
    // LlglIndexBufferBackend
    // -----------------------------------------------------------------------------------------

    LlglIndexBufferBackend::LlglIndexBufferBackend(LLGL::RenderSystem* renderSystem,
                                                    LlglGraphicsBackend* owner, int indexCapacity,
                                                    bool thirtyTwoBit)
        : renderSystem_(renderSystem)
        , owner_(owner)
        , indexCapacity_(indexCapacity > 0 ? indexCapacity : 0)
        , thirtyTwoBit_(thirtyTwoBit)
    {
        if (renderSystem_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: null render system for index buffer");
    }

    LlglIndexBufferBackend::~LlglIndexBufferBackend()
    {
        if (buffer_ == nullptr)
            return;

        // Same deferral as the vertex buffer above, for the same reason.
        if (owner_ != nullptr)
            owner_->ScheduleBufferReleaseEXT(buffer_);
        else if (renderSystem_ != nullptr)
            renderSystem_->Release(*buffer_);
    }

    void LlglIndexBufferBackend::Upload(const void* data, int indexCount, std::size_t indexSize)
    {
        if (data == nullptr || indexCount <= 0)
        {
            indexCount_ = 0;
            return;
        }

        const std::size_t byteSize = static_cast<std::size_t>(indexCount) * indexSize;
        if (buffer_ == nullptr || byteSize > byteCapacity_)
        {
            if (buffer_ != nullptr)
                renderSystem_->Release(*buffer_);

            LLGL::BufferDescriptor bufferDesc;
            bufferDesc.size = byteSize;
            bufferDesc.bindFlags = LLGL::BindFlags::IndexBuffer;
            bufferDesc.format = (indexSize == 4 ? LLGL::Format::R32UInt : LLGL::Format::R16UInt);
            buffer_ = renderSystem_->CreateBuffer(bufferDesc, data);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string(kBackendName) + " backend: index buffer creation failed");
            byteCapacity_ = byteSize;
        }
        else
        {
            renderSystem_->WriteBuffer(*buffer_, 0, data, byteSize);
        }

        indexCount_ = indexCount;
    }

    void LlglIndexBufferBackend::SetData16(const void* data, int indexCount)
    {
        if (thirtyTwoBit_)
            throw std::runtime_error(std::string(kBackendName) + " backend: SetData16 on a 32-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void LlglIndexBufferBackend::SetData32(const void* data, int indexCount)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error(std::string(kBackendName) + " backend: SetData32 on a 16-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    // -----------------------------------------------------------------------------------------
    // LlglSpriteBatchBackend
    // -----------------------------------------------------------------------------------------

    LlglSpriteBatchBackend::LlglSpriteBatchBackend(LlglGraphicsBackend& owner)
        : owner_(owner)
        , transform_(Matrix::getIdentityProperty())
    {
    }

    void LlglSpriteBatchBackend::Begin()
    {
        transform_ = Matrix::getIdentityProperty();
    }

    void LlglSpriteBatchBackend::End()
    {
        // Nothing to flush: every Draw already appended its geometry to the owning backend's
        // frame, which is recorded and submitted as a single command buffer at Present().
    }

    void LlglSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void LlglSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        textureFilter_ = textureFilter;
    }

    void LlglSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void LlglSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y),
                                    texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void LlglSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
             SpriteEffects::None, 0.0f);
    }

    void LlglSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float /*layerDepth*/)
    {
        const auto* llglTexture = dynamic_cast<const LlglTextureBackend*>(&texture);
        if (llglTexture == nullptr)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: SpriteBatch was given a texture from another backend");
        }

        owner_.QueueSpriteEXT(*llglTexture, destinationRectangle, sourceRectangle, color, rotation,
                              origin, effects, transform_, textureFilter_, addressU_, addressV_);
    }

    // -----------------------------------------------------------------------------------------
    // LlglGraphicsBackend
    // -----------------------------------------------------------------------------------------

    LlglGraphicsBackend::LlglGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        window_ = args.window;
        if (window_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: no SDL window was supplied");

        presentationMode_ = static_cast<int>(args.presentationMode);
        swapInterval_ = args.swapInterval;
        requestedSampleCount_ = args.multiSampleCount > 1 ? args.multiSampleCount : 1;

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSizeInPixels(window_, &windowWidth, &windowHeight);
        virtualWidth_ = args.virtualWidth > 0 ? args.virtualWidth : windowWidth;
        virtualHeight_ = args.virtualHeight > 0 ? args.virtualHeight : windowHeight;

        module_ = Detail::ResolveRendererModule();

        LLGL::RenderSystemDescriptor rendererDesc;
        rendererDesc.moduleName = Detail::GetRendererModuleName(module_);

        // NOXNA diagnostics: CNA_LLGL_DEBUG=1 turns on LLGL's own debug layer and routes its
        // reports to stdout. Off by default -- the debug layer validates every command and is far
        // too costly to leave on -- but invaluable when a draw silently produces nothing.
        static LLGL::RenderingDebugger debugger;
        const char* debugRequest = SDL_getenv("CNA_LLGL_DEBUG");
        if (debugRequest != nullptr && debugRequest[0] == '1')
        {
            LLGL::Log::RegisterCallbackStd();
            rendererDesc.flags |= LLGL::RenderSystemFlags::DebugDevice;
            rendererDesc.debugger = &debugger;
        }

        LLGL::Report report;
        renderer_ = LLGL::RenderSystem::Load(rendererDesc, &report);
        if (!renderer_)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: failed to load the " +
                Detail::GetRendererModuleName(module_) + " module (" +
                (report.GetText() != nullptr ? report.GetText() : "no details") + ")");
        }

        surface_ = std::make_shared<LlglSdlSurface>(window_);

        LLGL::SwapChainDescriptor swapChainDesc;
        swapChainDesc.resolution = surface_->GetContentSize();
        swapChainDesc.depthBits = 24;
        swapChainDesc.stencilBits = 8;
        swapChainDesc.samples = static_cast<std::uint32_t>(requestedSampleCount_);
        swapChainDesc.fullscreen = args.isFullScreen;

        swapChain_ = renderer_->CreateSwapChain(swapChainDesc, surface_);
        if (swapChain_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: swap chain creation failed");

        swapChain_->SetVsyncInterval(static_cast<std::uint32_t>(swapInterval_ > 0 ? swapInterval_ : 0));

        queue_ = renderer_->GetCommandQueue();
        commands_ = renderer_->CreateCommandBuffer();
        if (queue_ == nullptr || commands_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: command buffer creation failed");

        CreateSpritePipelineResources();
        CreatePrimitivePipelineResources();

        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    LlglGraphicsBackend::~LlglGraphicsBackend()
    {
        if (window_ != nullptr)
            IGraphicsBackend::UnregisterForWindow(window_);

        if (!renderer_)
            return;

        for (const auto& entry : pipelineCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        pipelineCache_.clear();

        for (const auto& entry : samplerCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        samplerCache_.clear();

        for (const auto& entry : primitivePipelineCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitivePipelineCache_.clear();

        for (const auto& entry : primitiveVertexShaderCache_)
        {
            if (entry.second != nullptr)
                renderer_->Release(*entry.second);
        }
        primitiveVertexShaderCache_.clear();

        for (LLGL::Buffer* buffer : pendingBufferReleases_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        pendingBufferReleases_.clear();

        for (LLGL::Buffer* buffer : transformBuffers_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        transformBuffers_.clear();

        if (primitiveFragmentShader_ != nullptr)
            renderer_->Release(*primitiveFragmentShader_);
        if (primitiveTexturedFragmentShader_ != nullptr)
            renderer_->Release(*primitiveTexturedFragmentShader_);
        if (primitiveLayout_ != nullptr)
            renderer_->Release(*primitiveLayout_);
        if (primitiveTexturedLayout_ != nullptr)
            renderer_->Release(*primitiveTexturedLayout_);

        if (spriteVertexBuffer_ != nullptr)
            renderer_->Release(*spriteVertexBuffer_);
        if (spriteProjectionBuffer_ != nullptr)
            renderer_->Release(*spriteProjectionBuffer_);
        if (spriteLayout_ != nullptr)
            renderer_->Release(*spriteLayout_);
        if (spriteVertexShader_ != nullptr)
            renderer_->Release(*spriteVertexShader_);
        if (spriteFragmentShader_ != nullptr)
            renderer_->Release(*spriteFragmentShader_);
        if (commands_ != nullptr)
            renderer_->Release(*commands_);
        if (swapChain_ != nullptr)
            renderer_->Release(*swapChain_);
    }

    const char* LlglGraphicsBackend::GetRendererNameEXT() const
    {
        if (!renderer_)
            return Detail::GetRendererModuleName(module_);
        return renderer_->GetRendererInfo().rendererName.c_str();
    }

    void LlglGraphicsBackend::CreateSpritePipelineResources()
    {
        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();
        const LLGL::VertexFormat vertexFormat = MakeSpriteVertexFormat();

        LLGL::ShaderDescriptor vertexDesc;
        LLGL::ShaderDescriptor fragmentDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        fragmentDesc.type = LLGL::ShaderType::Fragment;

        // The shading language the loaded module reports decides which of the two checked-in
        // shader flavours is used -- not the module name -- so a module that gains or loses a
        // language cannot end up handed a form it never accepted.
        //
        // GLSL is checked FIRST, and that order is load-bearing rather than cosmetic. A modern
        // OpenGL module reports BOTH languages, because desktop GL can ingest SPIR-V through
        // GL_ARB_gl_spirv -- but the SPIR-V shipped here was compiled for Vulkan's binding model,
        // and GL accepts it far enough to be dangerous: the position attribute still arrives, so
        // geometry rasterizes in the right place while the uniform block and every other attribute
        // silently read as zero. That is what made the OpenGL module clear correctly and draw
        // nothing at all (LLGL-17). Preferring GLSL wherever it exists keeps each module on the
        // form its own binding model was authored against; SPIR-V is the fallback for a module
        // that has no GLSL at all, which is exactly Vulkan.
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = Shaders::kSprite2dVertGlsl;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;

            fragmentDesc.source = Shaders::kSprite2dFragGlsl;
            fragmentDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::SPIRV))
        {
            vertexDesc.source = reinterpret_cast<const char*>(Shaders::kSprite2dVertSpv);
            vertexDesc.sourceSize = sizeof(Shaders::kSprite2dVertSpv);
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";

            fragmentDesc.source = reinterpret_cast<const char*>(Shaders::kSprite2dFragSpv);
            fragmentDesc.sourceSize = sizeof(Shaders::kSprite2dFragSpv);
            fragmentDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            fragmentDesc.entryPoint = "main";
        }
        else
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: the " + Detail::GetRendererModuleName(module_) +
                " module accepts neither SPIR-V nor GLSL, and this backend ships no other shader form");
        }

        vertexDesc.vertex.inputAttribs = vertexFormat.attributes;

        spriteVertexShader_ = renderer_->CreateShader(vertexDesc);
        spriteFragmentShader_ = renderer_->CreateShader(fragmentDesc);

        for (LLGL::Shader* shader : {spriteVertexShader_, spriteFragmentShader_})
        {
            if (shader == nullptr)
                throw std::runtime_error(std::string(kBackendName) + " backend: sprite shader creation failed");
            if (const LLGL::Report* shaderReport = shader->GetReport())
            {
                if (shaderReport->HasErrors())
                {
                    throw std::runtime_error(
                        std::string(kBackendName) + " backend: sprite shader compilation failed: " +
                        (shaderReport->GetText() != nullptr ? shaderReport->GetText() : "no details"));
                }
            }
        }

        LLGL::PipelineLayoutDescriptor layoutDesc;
        layoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Scene", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer, LLGL::StageFlags::VertexStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
        };
        // Desktop GLSL has no separate texture and sampler objects, so the OpenGL flavour of the
        // fragment shader declares a single combined sampler2D; this entry maps it back onto the
        // texture/sampler pair the Vulkan flavour keeps apart.
        layoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2}
        };
        spriteLayout_ = renderer_->CreatePipelineLayout(layoutDesc);
        if (spriteLayout_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: sprite pipeline layout creation failed");

        LLGL::BufferDescriptor projectionDesc;
        projectionDesc.size = sizeof(float) * 16;
        projectionDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
        spriteProjectionBuffer_ = renderer_->CreateBuffer(projectionDesc);
        if (spriteProjectionBuffer_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: sprite constant buffer creation failed");
    }

    bool LlglGraphicsBackend::SupportsWireFrameEXT() const
    {
        // Module-dependent, and LLGL exposes no capability flag for it, so this is the empirical
        // answer: verified drawing real wireframe edges on the OpenGL module, verified drawing
        // nothing at all on the Vulkan one.
        return module_ == Detail::RendererModule::OpenGL;
    }

    bool LlglGraphicsBackend::IsOpaqueBlendState() const
    {
        // XNA has no separate "blending on" switch: BlendState.Opaque is expressed as One/Zero
        // with Add, arithmetically identical to blending being off. Deriving the enable bit from
        // the factors themselves keeps both representations in agreement without depending on
        // whether SetBlendEnabled() happened to be called first.
        return static_cast<XnaBlend>(colorSrcBlend_) == XnaBlend::One &&
               static_cast<XnaBlend>(colorDstBlend_) == XnaBlend::Zero &&
               static_cast<XnaBlend>(alphaSrcBlend_) == XnaBlend::One &&
               static_cast<XnaBlend>(alphaDstBlend_) == XnaBlend::Zero &&
               static_cast<XnaBlendFunction>(colorBlendFunc_) == XnaBlendFunction::Add &&
               static_cast<XnaBlendFunction>(alphaBlendFunc_) == XnaBlendFunction::Add;
    }

    bool LlglGraphicsBackend::UsesConstantBlendFactorState() const
    {
        return UsesConstantBlendFactor(colorSrcBlend_) || UsesConstantBlendFactor(colorDstBlend_) ||
               UsesConstantBlendFactor(alphaSrcBlend_) || UsesConstantBlendFactor(alphaDstBlend_);
    }

    void LlglGraphicsBackend::CreatePrimitivePipelineResources()
    {
        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();
        const bool useGlsl = SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL);

        const auto createFragmentShader = [&](const char* glslSource, const std::uint32_t* spirv,
                                              std::size_t spirvSize, const char* what) {
            LLGL::ShaderDescriptor fragmentDesc;
            fragmentDesc.type = LLGL::ShaderType::Fragment;
            if (useGlsl)
            {
                fragmentDesc.source = glslSource;
                fragmentDesc.sourceType = LLGL::ShaderSourceType::CodeString;
            }
            else
            {
                fragmentDesc.source = reinterpret_cast<const char*>(spirv);
                fragmentDesc.sourceSize = spirvSize;
                fragmentDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
                fragmentDesc.entryPoint = "main";
            }

            LLGL::Shader* shader = renderer_->CreateShader(fragmentDesc);
            if (shader == nullptr)
                throw std::runtime_error(std::string(kBackendName) + " backend: " + what + " creation failed");
            if (const LLGL::Report* report = shader->GetReport())
            {
                if (report->HasErrors())
                {
                    throw std::runtime_error(
                        std::string(kBackendName) + " backend: " + what + " compilation failed: " +
                        (report->GetText() != nullptr ? report->GetText() : "no details"));
                }
            }
            return shader;
        };

        primitiveFragmentShader_ = createFragmentShader(
            Shaders::kUntextured3dFragGlsl, Shaders::kUntextured3dFragSpv,
            sizeof(Shaders::kUntextured3dFragSpv), "untextured 3D fragment shader");
        primitiveTexturedFragmentShader_ = createFragmentShader(
            Shaders::kTextured3dFragGlsl, Shaders::kTextured3dFragSpv,
            sizeof(Shaders::kTextured3dFragSpv), "textured 3D fragment shader");

        // Two layouts rather than one with an optionally-unused texture slot: a pipeline whose
        // layout declares a texture the draw never binds is a validation error on Vulkan, not a
        // harmless spare binding.
        LLGL::PipelineLayoutDescriptor layoutDesc;
        layoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Transform", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
        };
        primitiveLayout_ = renderer_->CreatePipelineLayout(layoutDesc);

        LLGL::PipelineLayoutDescriptor texturedLayoutDesc;
        texturedLayoutDesc.bindings =
        {
            LLGL::BindingDescriptor{"Transform", LLGL::ResourceType::Buffer,
                                    LLGL::BindFlags::ConstantBuffer,
                                    LLGL::StageFlags::VertexStage | LLGL::StageFlags::FragmentStage, 1},
            LLGL::BindingDescriptor{"colorMap", LLGL::ResourceType::Texture,
                                    LLGL::BindFlags::Sampled, LLGL::StageFlags::FragmentStage, 2},
            LLGL::BindingDescriptor{"samplerState", LLGL::ResourceType::Sampler,
                                    0, LLGL::StageFlags::FragmentStage, 3},
        };
        texturedLayoutDesc.combinedTextureSamplers =
        {
            LLGL::CombinedTextureSamplerDescriptor{"colorMap", "colorMap", "samplerState", 2}
        };
        primitiveTexturedLayout_ = renderer_->CreatePipelineLayout(texturedLayoutDesc);

        if (primitiveLayout_ == nullptr || primitiveTexturedLayout_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: 3D pipeline layout creation failed");
    }

    LLGL::Shader* LlglGraphicsBackend::AcquirePrimitiveVertexShader(
        const std::vector<LLGL::VertexAttribute>& attributes, bool textured)
    {
        const std::uint64_t key = MakeVertexLayoutKey(attributes) * 2u + (textured ? 1u : 0u);
        const auto cached = primitiveVertexShaderCache_.find(key);
        if (cached != primitiveVertexShaderCache_.end())
            return cached->second;

        // Which attributes the layout actually carries decides the shader variant: a shader that
        // declares an input the buffer does not supply reads undefined data on Vulkan, so the
        // variant is chosen from the layout rather than from what the effect asked for.
        bool hasColor = false;
        bool hasTexCoord = false;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 1) hasColor = true;
            if (attribute.location == 2) hasTexCoord = true;
        }

        if (textured && !hasTexCoord)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: a textured effect needs a vertex layout with "
                "texture coordinates, and this one has none");
        }

        const char* glslSource = nullptr;
        const std::uint32_t* spirv = nullptr;
        std::size_t spirvSize = 0;
        if (textured)
        {
            if (hasColor)
            {
                glslSource = Shaders::kColoredTextured3dVertGlsl;
                spirv = Shaders::kColoredTextured3dVertSpv;
                spirvSize = sizeof(Shaders::kColoredTextured3dVertSpv);
            }
            else
            {
                glslSource = Shaders::kTextured3dVertGlsl;
                spirv = Shaders::kTextured3dVertSpv;
                spirvSize = sizeof(Shaders::kTextured3dVertSpv);
            }
        }
        else if (hasColor)
        {
            glslSource = Shaders::kColored3dVertGlsl;
            spirv = Shaders::kColored3dVertSpv;
            spirvSize = sizeof(Shaders::kColored3dVertSpv);
        }
        else
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: an untextured draw needs vertex colours, and "
                "this vertex layout has none");
        }

        // The attribute list handed to the shader is trimmed to what that variant declares: an
        // untextured draw from a layout that happens to carry texture coordinates must not leave
        // the pipeline expecting an input its shader never reads.
        std::vector<LLGL::VertexAttribute> shaderAttributes;
        for (const LLGL::VertexAttribute& attribute : attributes)
        {
            if (attribute.location == 2 && !textured)
                continue;
            shaderAttributes.push_back(attribute);
        }

        const LLGL::RenderingCapabilities& caps = renderer_->GetRenderingCaps();

        LLGL::ShaderDescriptor vertexDesc;
        vertexDesc.type = LLGL::ShaderType::Vertex;
        if (SupportsShadingLanguage(caps, LLGL::ShadingLanguage::GLSL))
        {
            vertexDesc.source = glslSource;
            vertexDesc.sourceType = LLGL::ShaderSourceType::CodeString;
        }
        else
        {
            vertexDesc.source = reinterpret_cast<const char*>(spirv);
            vertexDesc.sourceSize = spirvSize;
            vertexDesc.sourceType = LLGL::ShaderSourceType::BinaryBuffer;
            vertexDesc.entryPoint = "main";
        }
        vertexDesc.vertex.inputAttribs = shaderAttributes;

        LLGL::Shader* shader = renderer_->CreateShader(vertexDesc);
        if (shader == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: 3D vertex shader creation failed");
        if (const LLGL::Report* report = shader->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kBackendName) + " backend: 3D vertex shader compilation failed: " +
                    (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitiveVertexShaderCache_.emplace(key, shader);
        return shader;
    }

    LLGL::PipelineState* LlglGraphicsBackend::AcquirePrimitivePipeline(
        const LlglVertexBufferBackend& vertexBuffer, PrimitiveType primitive, bool scissorEnabled,
        bool textured)
    {
        const std::vector<LLGL::VertexAttribute>& attributes = vertexBuffer.GetVertexAttributes();

        std::uint64_t key = MakeVertexLayoutKey(attributes);
        key = key * 8u + static_cast<std::uint64_t>(static_cast<int>(primitive) & 0x7);
        key = key * 2u + (depthTestEnabled_ ? 1u : 0u);
        key = key * 2u + (depthWriteEnabled_ ? 1u : 0u);
        key = key * 8u + static_cast<std::uint64_t>(depthCompareFunction_ & 0x7);
        key = key * 4u + static_cast<std::uint64_t>(cullMode_ & 0x3);
        key = key * 2u + static_cast<std::uint64_t>(fillMode_ & 0x1);
        key = key * 2u + (scissorEnabled ? 1u : 0u);
        key = key * 2u + (textured ? 1u : 0u);
        key = key * 1024u + (MakeBlendPipelineKey(scissorEnabled) & 0x3FFu);

        const auto cached = primitivePipelineCache_.find(key);
        if (cached != primitivePipelineCache_.end())
            return cached->second;

        LLGL::GraphicsPipelineDescriptor pipelineDesc;
        pipelineDesc.debugName = textured ? "CNA.Textured3D" : "CNA.Colored3D";
        pipelineDesc.vertexShader = AcquirePrimitiveVertexShader(attributes, textured);
        pipelineDesc.fragmentShader = textured ? primitiveTexturedFragmentShader_
                                               : primitiveFragmentShader_;
        pipelineDesc.pipelineLayout = textured ? primitiveTexturedLayout_ : primitiveLayout_;
        pipelineDesc.renderPass = swapChain_->GetRenderPass();
        pipelineDesc.primitiveTopology = MapPrimitiveTopology(primitive);
        pipelineDesc.depth.testEnabled = depthTestEnabled_;
        pipelineDesc.depth.writeEnabled = depthWriteEnabled_;
        pipelineDesc.depth.compareOp = MapCompareFunction(depthCompareFunction_);
        pipelineDesc.rasterizer.multiSampleEnabled = (swapChain_->GetSamples() > 1);
        pipelineDesc.rasterizer.scissorTestEnabled = scissorEnabled;
        pipelineDesc.rasterizer.cullMode = MapCullMode(cullMode_);
        if (static_cast<XnaFillMode>(fillMode_) == XnaFillMode::WireFrame && !SupportsWireFrameEXT())
        {
            // LLGL's Vulkan module does not enable the device feature a line polygon mode needs, and
            // the request does not fail there -- it draws nothing at all, neither edges nor fill
            // (measured, not assumed). Silently rendering an empty frame for a state the caller
            // explicitly asked for is exactly the failure mode this project refuses.
            NotYetImplemented(kBackendName, "FillMode::WireFrame on the Vulkan renderer module");
        }
        pipelineDesc.rasterizer.polygonMode = MapFillMode(fillMode_);
        // XNA calls a clockwise winding front-facing, and it means clockwise ON SCREEN. Two Y
        // flips cancel out on the way here -- LLGL's clip space is Y-up, and its Vulkan viewport is
        // submitted with a negated height -- so the winding the rasterizer sees is the winding on
        // screen, and front-facing is simply "not counter-clockwise". Verified rather than
        // reasoned: with frontCCW true, CullClockwiseFace left a screen-clockwise triangle on
        // screen, which is the opposite of what XNA does.
        pipelineDesc.rasterizer.frontCCW = false;
        pipelineDesc.blend.blendFactorDynamic = UsesConstantBlendFactorState();
        pipelineDesc.blend.targets[0].blendEnabled = !IsOpaqueBlendState();
        pipelineDesc.blend.targets[0].srcColor = MapBlendFactor(colorSrcBlend_);
        pipelineDesc.blend.targets[0].dstColor = MapBlendFactor(colorDstBlend_);
        pipelineDesc.blend.targets[0].colorArithmetic = MapBlendFunction(colorBlendFunc_);
        pipelineDesc.blend.targets[0].srcAlpha = MapBlendFactor(alphaSrcBlend_);
        pipelineDesc.blend.targets[0].dstAlpha = MapBlendFactor(alphaDstBlend_);
        pipelineDesc.blend.targets[0].alphaArithmetic = MapBlendFunction(alphaBlendFunc_);
        pipelineDesc.blend.targets[0].colorMask = MapColorWriteMask(colorWriteChannels_);

        LLGL::PipelineState* pipeline = renderer_->CreatePipelineState(pipelineDesc);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: 3D pipeline creation failed");
        if (const LLGL::Report* report = pipeline->GetReport())
        {
            if (report->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kBackendName) + " backend: 3D pipeline link failed: " +
                    (report->GetText() != nullptr ? report->GetText() : "no details"));
            }
        }

        primitivePipelineCache_.emplace(key, pipeline);
        return pipeline;
    }

    void LlglGraphicsBackend::FillEffectUniforms(float (&uniforms)[32], const float matrix[16],
                                                  const GpuDrawParams* params)
    {
        std::memcpy(uniforms, matrix, sizeof(float) * 16);

        // Defaults are the "no effect state at all" values: opaque white, fog off, and an alpha
        // test that always passes -- the same neutral values the shaders would see from a stock
        // effect that enables none of these.
        uniforms[16] = 1.0f; uniforms[17] = 1.0f; uniforms[18] = 1.0f; uniforms[19] = 1.0f;
        uniforms[20] = 0.0f; uniforms[21] = 0.0f; uniforms[22] = 0.0f; uniforms[23] = 0.0f;
        uniforms[24] = 0.0f; uniforms[25] = 0.0f; uniforms[26] = 0.0f; uniforms[27] = 0.0f;
        uniforms[28] = 0.0f; uniforms[29] = 0.0f; uniforms[30] = 1.0f; uniforms[31] = 1.0f;

        if (params == nullptr)
            return;

        uniforms[16] = params->diffuseColor[0];
        uniforms[17] = params->diffuseColor[1];
        uniforms[18] = params->diffuseColor[2];
        uniforms[19] = params->diffuseColor[3];

        if (params->fogEnabled)
        {
            uniforms[20] = params->fogColor[0];
            uniforms[21] = params->fogColor[1];
            uniforms[22] = params->fogColor[2];
            // The alpha channel carries the enable bit, so a disabled fog multiplies the whole
            // factor by zero in the shader instead of needing a branch.
            uniforms[23] = 1.0f;
            uniforms[24] = params->fogVector[0];
            uniforms[25] = params->fogVector[1];
            uniforms[26] = params->fogVector[2];
            uniforms[27] = params->fogVector[3];
        }

        uniforms[28] = params->alphaTest[0];
        uniforms[29] = params->alphaTest[1];
        uniforms[30] = params->alphaTest[2];
        uniforms[31] = params->alphaTest[3];
    }

    void LlglGraphicsBackend::QueuePrimitives(const LlglVertexBufferBackend& vertexBuffer,
                                               const LlglIndexBufferBackend* indexBuffer,
                                               const Matrix& world, const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive, int primitiveCount,
                                               int vertexStart, int startIndex, int baseVertex,
                                               const GpuDrawParams* params)
    {
        if (primitiveCount <= 0)
            return;

        if (vertexBuffer.GetLlglBuffer() == nullptr || vertexBuffer.GetVertexCount() <= 0)
            throw std::runtime_error(std::string(kBackendName) + " backend: the vertex buffer holds no data");

        const std::vector<LLGL::VertexAttribute>& attributes = vertexBuffer.GetVertexAttributes();
        if (attributes.empty())
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: this vertex layout is not supported by the "
                "colour-only 3D path (stride " + std::to_string(vertexBuffer.GetStride()) +
                " with no vertex declaration). Supply a VertexDeclaration, or use a "
                "VertexPositionColor layout.");
        }

        const int elementCount = CountElementsForPrimitives(primitive, primitiveCount);

        // The matrix is combined here, once per draw, in XNA's own row-vector order. ToColumnMajor
        // then writes the row-major storage out verbatim, which GLSL reads as column-major -- the
        // transpose, i.e. exactly the column-vector form the shader multiplies with.
        const Matrix combined = world * view * projection;
        float matrix[16] = {};
        combined.ToColumnMajor(matrix);

        // An XNA projection puts depth in [0,1] (the Direct3D convention). A render system whose
        // clip space is [-1,+1] would compress every scene into the upper half of its depth range
        // and stop clipping anything between the camera and the near plane, so the correction is
        // folded into this matrix rather than duplicated in a second shader variant.
        if (renderer_->GetRenderingCaps().clippingRange == LLGL::ClippingRange::MinusOneToOne)
        {
            for (int column = 0; column < 4; ++column)
            {
                float& z = matrix[column * 4 + 2];
                const float w = matrix[column * 4 + 3];
                z = 2.0f * z - w;
            }
        }

        const bool textured = (params != nullptr && params->textureEnabled && params->texture0 != nullptr);
        const LlglTextureBackend* texture = nullptr;
        if (textured)
        {
            texture = dynamic_cast<const LlglTextureBackend*>(params->texture0);
            if (texture == nullptr || texture->GetLlglTexture() == nullptr)
            {
                throw std::runtime_error(
                    std::string(kBackendName) + " backend: the effect's texture belongs to another backend");
            }
        }

        float uniforms[kEffectUniformFloats] = {};
        FillEffectUniforms(uniforms, matrix, params);

        const auto transformIndex =
            static_cast<std::uint32_t>(transformData_.size() / kEffectUniformFloats);
        transformData_.insert(transformData_.end(), std::begin(uniforms), std::end(uniforms));

        std::int32_t scissor[4] = {0, 0, 0, 0};
        const bool scissorEnabled = ComputeEffectiveScissor(scissor);

        FrameCommand command;
        command.kind = FrameCommand::Kind::Primitives;
        command.vertexBuffer = vertexBuffer.GetLlglBuffer();
        command.pipeline = AcquirePrimitivePipeline(vertexBuffer, primitive, scissorEnabled, textured);
        if (textured)
        {
            command.texture = texture->GetLlglTexture();
            command.sampler = AcquireSampler(samplerFilter_, samplerAddressU_, samplerAddressV_,
                                             samplerMaxAnisotropy_);
        }
        command.transformIndex = transformIndex;
        command.vertexCount = static_cast<std::uint32_t>(elementCount);
        command.firstVertex = static_cast<std::uint32_t>(std::max(0, vertexStart));
        command.firstIndex = static_cast<std::uint32_t>(std::max(0, startIndex));
        command.baseVertex = baseVertex;
        std::memcpy(command.blendFactor, blendFactor_, sizeof(command.blendFactor));
        command.usesBlendFactor = UsesConstantBlendFactorState();
        std::memcpy(command.scissor, scissor, sizeof(command.scissor));
        command.scissorEnabled = scissorEnabled;

        if (indexBuffer != nullptr)
        {
            if (indexBuffer->GetLlglBuffer() == nullptr || indexBuffer->GetIndexCount() <= 0)
                throw std::runtime_error(std::string(kBackendName) + " backend: the index buffer holds no data");
            if (startIndex + elementCount > indexBuffer->GetIndexCount())
            {
                throw std::runtime_error(
                    std::string(kBackendName) + " backend: the draw needs indices [" +
                    std::to_string(startIndex) + ", " + std::to_string(startIndex + elementCount) +
                    ") but the buffer holds " + std::to_string(indexBuffer->GetIndexCount()));
            }
            command.indexBuffer = indexBuffer->GetLlglBuffer();
        }
        else if (vertexStart + elementCount > vertexBuffer.GetVertexCount())
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: the draw needs vertices [" +
                std::to_string(vertexStart) + ", " + std::to_string(vertexStart + elementCount) +
                ") but the buffer holds " + std::to_string(vertexBuffer.GetVertexCount()));
        }

        frameCommands_.push_back(command);
        backbufferCacheValid_ = false;
    }

    std::uint64_t LlglGraphicsBackend::MakeBlendPipelineKey(bool scissorEnabled) const
    {
        // Packs every field the sprite pipeline is built from, so a cache hit really is the same
        // pipeline: four blend factors, two blend functions, the colour write mask, and scissor.
        std::uint64_t key = 0;
        key = key * 16u + static_cast<std::uint64_t>(colorSrcBlend_ & 0xF);
        key = key * 16u + static_cast<std::uint64_t>(colorDstBlend_ & 0xF);
        key = key * 16u + static_cast<std::uint64_t>(alphaSrcBlend_ & 0xF);
        key = key * 16u + static_cast<std::uint64_t>(alphaDstBlend_ & 0xF);
        key = key * 8u + static_cast<std::uint64_t>(colorBlendFunc_ & 0x7);
        key = key * 8u + static_cast<std::uint64_t>(alphaBlendFunc_ & 0x7);
        key = key * 16u + static_cast<std::uint64_t>(colorWriteChannels_ & 0xF);
        key = key * 2u + (scissorEnabled ? 1u : 0u);
        return key;
    }

    LLGL::PipelineState* LlglGraphicsBackend::AcquireSpritePipeline(bool scissorEnabled)
    {
        const std::uint64_t key = MakeBlendPipelineKey(scissorEnabled);
        const auto cached = pipelineCache_.find(key);
        if (cached != pipelineCache_.end())
            return cached->second;

        LLGL::GraphicsPipelineDescriptor pipelineDesc;
        pipelineDesc.debugName = "CNA.Sprite";
        pipelineDesc.vertexShader = spriteVertexShader_;
        pipelineDesc.fragmentShader = spriteFragmentShader_;
        pipelineDesc.pipelineLayout = spriteLayout_;
        pipelineDesc.renderPass = swapChain_->GetRenderPass();
        pipelineDesc.primitiveTopology = LLGL::PrimitiveTopology::TriangleList;
        pipelineDesc.depth.testEnabled = false;
        pipelineDesc.depth.writeEnabled = false;
        pipelineDesc.rasterizer.multiSampleEnabled = (swapChain_->GetSamples() > 1);
        pipelineDesc.rasterizer.scissorTestEnabled = scissorEnabled;
        pipelineDesc.rasterizer.cullMode = LLGL::CullMode::Disabled;
        // Dynamic blend-factor state is requested only when the blend state actually references the
        // constant blend colour. It is not free: LLGL implements it on OpenGL with glBlendColor,
        // which is genuinely absent from some GL proc tables (this project's own software-rasterized
        // test environment among them) and throws rather than being ignored (LLGL-18). Asking for it
        // unconditionally would make every draw depend on a call that the overwhelming majority of
        // blend states have no use for.
        pipelineDesc.blend.blendFactorDynamic = UsesConstantBlendFactorState();
        pipelineDesc.blend.targets[0].blendEnabled = !IsOpaqueBlendState();
        pipelineDesc.blend.targets[0].srcColor = MapBlendFactor(colorSrcBlend_);
        pipelineDesc.blend.targets[0].dstColor = MapBlendFactor(colorDstBlend_);
        pipelineDesc.blend.targets[0].colorArithmetic = MapBlendFunction(colorBlendFunc_);
        pipelineDesc.blend.targets[0].srcAlpha = MapBlendFactor(alphaSrcBlend_);
        pipelineDesc.blend.targets[0].dstAlpha = MapBlendFactor(alphaDstBlend_);
        pipelineDesc.blend.targets[0].alphaArithmetic = MapBlendFunction(alphaBlendFunc_);
        pipelineDesc.blend.targets[0].colorMask = MapColorWriteMask(colorWriteChannels_);

        LLGL::PipelineState* pipeline = renderer_->CreatePipelineState(pipelineDesc);
        if (pipeline == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: sprite pipeline creation failed");
        if (const LLGL::Report* pipelineReport = pipeline->GetReport())
        {
            if (pipelineReport->HasErrors())
            {
                throw std::runtime_error(
                    std::string(kBackendName) + " backend: sprite pipeline link failed: " +
                    (pipelineReport->GetText() != nullptr ? pipelineReport->GetText() : "no details"));
            }
        }

        pipelineCache_.emplace(key, pipeline);
        return pipeline;
    }

    LLGL::Sampler* LlglGraphicsBackend::AcquireSampler(int filter, int addressU, int addressV, int maxAnisotropy)
    {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(filter & 0xF) << 24) |
            (static_cast<std::uint64_t>(addressU & 0xF) << 20) |
            (static_cast<std::uint64_t>(addressV & 0xF) << 16) |
            static_cast<std::uint64_t>(maxAnisotropy & 0xFF);

        const auto cached = samplerCache_.find(key);
        if (cached != samplerCache_.end())
            return cached->second;

        LLGL::SamplerFilter minFilter = LLGL::SamplerFilter::Linear;
        LLGL::SamplerFilter magFilter = LLGL::SamplerFilter::Linear;
        LLGL::SamplerFilter mipFilter = LLGL::SamplerFilter::Linear;
        bool anisotropic = false;
        MapTextureFilter(filter, minFilter, magFilter, mipFilter, anisotropic);

        LLGL::SamplerDescriptor samplerDesc;
        samplerDesc.minFilter = minFilter;
        samplerDesc.magFilter = magFilter;
        samplerDesc.mipMapFilter = mipFilter;
        samplerDesc.addressModeU = MapAddressMode(addressU);
        samplerDesc.addressModeV = MapAddressMode(addressV);
        samplerDesc.addressModeW = LLGL::SamplerAddressMode::Clamp;
        samplerDesc.maxAnisotropy = anisotropic
            ? static_cast<std::uint32_t>(std::clamp(maxAnisotropy, 1, 16))
            : 1u;

        LLGL::Sampler* sampler = renderer_->CreateSampler(samplerDesc);
        if (sampler == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: sampler creation failed");

        samplerCache_.emplace(key, sampler);
        return sampler;
    }

    LlglGraphicsBackend::PresentationRect LlglGraphicsBackend::ComputePresentationRect() const
    {
        PresentationRect rect;

        const LLGL::Extent2D resolution = swapChain_ != nullptr
            ? swapChain_->GetResolution()
            : LLGL::Extent2D{0, 0};
        const float physicalWidth = static_cast<float>(resolution.width);
        const float physicalHeight = static_cast<float>(resolution.height);
        if (physicalWidth <= 0.0f || physicalHeight <= 0.0f)
            return rect;

        float logicalWidth = virtualWidth_ > 0 ? static_cast<float>(virtualWidth_) : physicalWidth;
        float logicalHeight = virtualHeight_ > 0 ? static_cast<float>(virtualHeight_) : physicalHeight;

        switch (static_cast<CnaPresentationMode>(presentationMode_))
        {
            case CnaPresentationMode::Stretch:
                rect.x = 0.0f;
                rect.y = 0.0f;
                rect.width = physicalWidth;
                rect.height = physicalHeight;
                break;

            case CnaPresentationMode::NativeBackBuffer:
                logicalWidth = physicalWidth;
                logicalHeight = physicalHeight;
                rect.x = 0.0f;
                rect.y = 0.0f;
                rect.width = physicalWidth;
                rect.height = physicalHeight;
                break;

            case CnaPresentationMode::Overscan:
            {
                const float scale = std::max(physicalWidth / logicalWidth, physicalHeight / logicalHeight);
                rect.width = logicalWidth * scale;
                rect.height = logicalHeight * scale;
                rect.x = (physicalWidth - rect.width) * 0.5f;
                rect.y = (physicalHeight - rect.height) * 0.5f;
                break;
            }

            case CnaPresentationMode::FixedHeightDynamicWidth:
            {
                // The preferred height stays fixed and the logical width follows the real aspect
                // ratio, so the canvas fills the surface exactly and a wider window simply shows
                // more horizontal content.
                logicalWidth = std::round(physicalWidth * logicalHeight / physicalHeight);
                if (logicalWidth <= 0.0f)
                    logicalWidth = physicalWidth;
                const float scale = std::min(physicalWidth / logicalWidth, physicalHeight / logicalHeight);
                rect.width = logicalWidth * scale;
                rect.height = logicalHeight * scale;
                rect.x = (physicalWidth - rect.width) * 0.5f;
                rect.y = (physicalHeight - rect.height) * 0.5f;
                break;
            }

            case CnaPresentationMode::Letterbox:
            default:
            {
                const float scale = std::min(physicalWidth / logicalWidth, physicalHeight / logicalHeight);
                rect.width = logicalWidth * scale;
                rect.height = logicalHeight * scale;
                rect.x = (physicalWidth - rect.width) * 0.5f;
                rect.y = (physicalHeight - rect.height) * 0.5f;
                break;
            }
        }

        rect.logicalWidth = logicalWidth;
        rect.logicalHeight = logicalHeight;
        return rect;
    }

    void LlglGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const PresentationRect rect = ComputePresentationRect();
        width = static_cast<int>(rect.logicalWidth);
        height = static_cast<int>(rect.logicalHeight);
    }

    void LlglGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        if (width > 0) virtualWidth_ = width;
        if (height > 0) virtualHeight_ = height;
    }

    void LlglGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = mode;
    }

    void LlglGraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = interval;
        if (swapChain_ != nullptr)
            swapChain_->SetVsyncInterval(static_cast<std::uint32_t>(interval > 0 ? interval : 0));
    }

    int LlglGraphicsBackend::GetMultiSampleCount() const
    {
        if (swapChain_ == nullptr)
            return 0;
        const std::uint32_t samples = swapChain_->GetSamples();
        return samples > 1 ? static_cast<int>(samples) : 0;
    }

    bool LlglGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                        float& logX, float& logY) const
    {
        const PresentationRect rect = ComputePresentationRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f)
            return false;

        logX = (windowX - rect.x) * rect.logicalWidth / rect.width;
        logY = (windowY - rect.y) * rect.logicalHeight / rect.height;
        return true;
    }

    bool LlglGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                        float& windowX, float& windowY) const
    {
        const PresentationRect rect = ComputePresentationRect();
        if (rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
            return false;

        windowX = rect.x + logX * rect.width / rect.logicalWidth;
        windowY = rect.y + logY * rect.height / rect.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureBackend> LlglGraphicsBackend::CreateTexture(const ImageData& data)
    {
        if (data.width <= 0 || data.height <= 0)
            throw std::runtime_error(std::string(kBackendName) + " backend: texture has no pixels");

        const std::size_t expected =
            static_cast<std::size_t>(data.width) * static_cast<std::size_t>(data.height) * 4u;

        LLGL::TextureDescriptor textureDesc;
        textureDesc.type = LLGL::TextureType::Texture2D;
        // Sampled for the sprite shader, plus the copy flags WriteTexture/ReadTexture need for
        // Texture2D::SetData()/GetData().
        textureDesc.bindFlags = LLGL::BindFlags::Sampled | LLGL::BindFlags::CopyDst |
                                LLGL::BindFlags::CopySrc;
        textureDesc.format = LLGL::Format::RGBA8UNorm;
        textureDesc.extent = {static_cast<std::uint32_t>(data.width),
                              static_cast<std::uint32_t>(data.height), 1};
        textureDesc.mipLevels = static_cast<std::uint32_t>(data.mipLevels > 0 ? data.mipLevels : 1);
        // No GenerateMips: the shared texture layer uploads each level it wants to exist, and
        // letting LLGL synthesise levels behind it would overwrite genuinely different content.
        textureDesc.miscFlags = 0;

        LLGL::ImageView imageView;
        LLGL::ImageView* initialImage = nullptr;
        if (data.pixels.size() >= expected)
        {
            imageView.format = LLGL::ImageFormat::RGBA;
            imageView.dataType = LLGL::DataType::UInt8;
            imageView.data = data.pixels.data();
            imageView.dataSize = expected;
            initialImage = &imageView;
        }

        LLGL::Texture* texture = renderer_->CreateTexture(textureDesc, initialImage);
        return std::make_unique<LlglTextureBackend>(renderer_.get(), texture, data.width, data.height,
                                                    static_cast<int>(textureDesc.mipLevels));
    }

    std::unique_ptr<ISpriteBatchBackend> LlglGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<LlglSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IVertexBufferBackend> LlglGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<LlglVertexBufferBackend>(renderer_.get(), this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> LlglGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<LlglIndexBufferBackend>(renderer_.get(), this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> LlglGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<LlglIndexBufferBackend>(renderer_.get(), this, index_capacity, true);
    }

    void LlglGraphicsBackend::QueueClear(long flags, const float color[4], float depth, std::uint32_t stencil)
    {
        FrameCommand command;
        command.kind = FrameCommand::Kind::Clear;
        command.clearFlags = flags;
        if (color != nullptr)
            std::memcpy(command.clearColor, color, sizeof(command.clearColor));
        command.clearDepth = depth;
        command.clearStencil = stencil;
        frameCommands_.push_back(command);
        backbufferCacheValid_ = false;
    }

    void LlglGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::Color, color, 1.0f, 0);
    }

    void LlglGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::ColorDepth, color, depth, 0);
    }

    void LlglGraphicsBackend::ClearDepth(float depth)
    {
        QueueClear(LLGL::ClearFlags::Depth, nullptr, depth, 0);
    }

    void LlglGraphicsBackend::ClearStencil(int stencil)
    {
        QueueClear(LLGL::ClearFlags::Stencil, nullptr, 1.0f, static_cast<std::uint32_t>(stencil));
    }

    void LlglGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        QueueClear(LLGL::ClearFlags::DepthStencil, nullptr, depth, static_cast<std::uint32_t>(stencil));
    }

    void LlglGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::Color | LLGL::ClearFlags::Stencil, color, 1.0f,
                   static_cast<std::uint32_t>(stencil));
    }

    void LlglGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                         float depth, int stencil)
    {
        const float color[4] = {r, g, b, a};
        QueueClear(LLGL::ClearFlags::All, color, depth, static_cast<std::uint32_t>(stencil));
    }

    bool LlglGraphicsBackend::ComputeEffectiveScissor(std::int32_t outRect[4]) const
    {
        const PresentationRect rect = ComputePresentationRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f ||
            rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
        {
            return false;
        }

        const float scaleX = rect.width / rect.logicalWidth;
        const float scaleY = rect.height / rect.logicalHeight;

        // A sub-viewport clips drawing in XNA. This backend keeps the GPU viewport at the whole
        // window (sprite geometry is already baked into window pixels) and expresses that clipping
        // through the scissor instead, intersected with any scissor the game set itself.
        float left = rect.x;
        float top = rect.y;
        float right = rect.x + rect.width;
        float bottom = rect.y + rect.height;
        bool restricted = false;

        if (viewportSet_)
        {
            left = std::max(left, rect.x + static_cast<float>(viewportRect_[0]) * scaleX);
            top = std::max(top, rect.y + static_cast<float>(viewportRect_[1]) * scaleY);
            right = std::min(right, rect.x + static_cast<float>(viewportRect_[0] + viewportRect_[2]) * scaleX);
            bottom = std::min(bottom, rect.y + static_cast<float>(viewportRect_[1] + viewportRect_[3]) * scaleY);
            restricted = true;
        }

        if (scissorTestEnabled_ && scissorRectSet_)
        {
            left = std::max(left, rect.x + static_cast<float>(scissorRect_[0]) * scaleX);
            top = std::max(top, rect.y + static_cast<float>(scissorRect_[1]) * scaleY);
            right = std::min(right, rect.x + static_cast<float>(scissorRect_[0] + scissorRect_[2]) * scaleX);
            bottom = std::min(bottom, rect.y + static_cast<float>(scissorRect_[1] + scissorRect_[3]) * scaleY);
            restricted = true;
        }

        if (!restricted)
            return false;

        outRect[0] = static_cast<std::int32_t>(std::lround(left));
        outRect[1] = static_cast<std::int32_t>(std::lround(top));
        outRect[2] = static_cast<std::int32_t>(std::max(0.0f, std::round(right - left)));
        outRect[3] = static_cast<std::int32_t>(std::max(0.0f, std::round(bottom - top)));
        return true;
    }

    void LlglGraphicsBackend::QueueSpriteEXT(const LlglTextureBackend& texture,
                                              const Rectangle& destination,
                                              const Rectangle& source,
                                              const Color& color,
                                              float rotation,
                                              const Vector2& origin,
                                              SpriteEffects effects,
                                              const Matrix& transform,
                                              int filter, int addressU, int addressV)
    {
        if (destination.Width == 0 || destination.Height == 0 ||
            source.Width == 0 || source.Height == 0 ||
            texture.GetWidth() <= 0 || texture.GetHeight() <= 0)
        {
            return;
        }

        const PresentationRect rect = ComputePresentationRect();
        if (rect.width <= 0.0f || rect.height <= 0.0f ||
            rect.logicalWidth <= 0.0f || rect.logicalHeight <= 0.0f)
        {
            return;
        }

        const float scaleX = static_cast<float>(destination.Width) / static_cast<float>(source.Width);
        const float scaleY = static_cast<float>(destination.Height) / static_cast<float>(source.Height);
        const float left = -origin.X * scaleX;
        const float top = -origin.Y * scaleY;
        const float right = left + static_cast<float>(destination.Width);
        const float bottom = top + static_cast<float>(destination.Height);

        std::array<Vector2, 4> points{
            Vector2{left, top}, Vector2{right, top}, Vector2{left, bottom}, Vector2{right, bottom}};

        const float sinRotation = std::sin(rotation);
        const float cosRotation = std::cos(rotation);
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * cosRotation - point.Y * sinRotation +
                                   static_cast<float>(destination.X);
            const float rotatedY = point.X * sinRotation + point.Y * cosRotation +
                                   static_cast<float>(destination.Y);
            point.X = rotatedX * transform.M11 + rotatedY * transform.M21 + transform.M41;
            point.Y = rotatedX * transform.M12 + rotatedY * transform.M22 + transform.M42;
        }

        float u0 = static_cast<float>(source.X) / static_cast<float>(texture.GetWidth());
        float v0 = static_cast<float>(source.Y) / static_cast<float>(texture.GetHeight());
        float u1 = static_cast<float>(source.X + source.Width) / static_cast<float>(texture.GetWidth());
        float v1 = static_cast<float>(source.Y + source.Height) / static_cast<float>(texture.GetHeight());

        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v0, v1);

        const std::array<Vector2, 4> uv{
            Vector2{u0, v0}, Vector2{u1, v0}, Vector2{u0, v1}, Vector2{u1, v1}};
        constexpr int kQuadIndices[kSpriteVerticesPerQuad] = {0, 1, 2, 2, 1, 3};

        const float rgba[4] = {
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        };

        const auto firstVertex = static_cast<std::uint32_t>(spriteVertexData_.size() / kSpriteVertexFloats);
        for (const int cornerIndex : kQuadIndices)
        {
            // Baked straight into window pixels: the letterbox offset and scale live in the
            // geometry, which leaves the GPU viewport free to stay at the full window and the
            // projection constant for the whole frame.
            const float px = rect.x + points[cornerIndex].X * rect.width / rect.logicalWidth;
            const float py = rect.y + points[cornerIndex].Y * rect.height / rect.logicalHeight;
            spriteVertexData_.insert(spriteVertexData_.end(),
                {px, py, uv[cornerIndex].X, uv[cornerIndex].Y, rgba[0], rgba[1], rgba[2], rgba[3]});
        }

        std::int32_t scissor[4] = {0, 0, 0, 0};
        const bool scissorEnabled = ComputeEffectiveScissor(scissor);

        FrameCommand command;
        command.kind = FrameCommand::Kind::Sprite;
        command.texture = texture.GetLlglTexture();
        command.sampler = AcquireSampler(filter, addressU, addressV, samplerMaxAnisotropy_);
        command.pipeline = AcquireSpritePipeline(scissorEnabled);
        command.firstVertex = firstVertex;
        command.vertexCount = static_cast<std::uint32_t>(kSpriteVerticesPerQuad);
        std::memcpy(command.blendFactor, blendFactor_, sizeof(command.blendFactor));
        command.usesBlendFactor = UsesConstantBlendFactorState();
        std::memcpy(command.scissor, scissor, sizeof(command.scissor));
        command.scissorEnabled = scissorEnabled;
        frameCommands_.push_back(command);
        backbufferCacheValid_ = false;
    }

    void LlglGraphicsBackend::UpdateSwapChainResolution()
    {
        if (swapChain_ == nullptr || !surface_)
            return;

        const LLGL::Extent2D contentSize = surface_->GetContentSize();
        if (contentSize.width == 0 || contentSize.height == 0)
            return;

        const LLGL::Extent2D current = swapChain_->GetResolution();
        if (contentSize.width != current.width || contentSize.height != current.height)
            swapChain_->ResizeBuffers(contentSize);
    }

    void LlglGraphicsBackend::UploadFrameResources()
    {
        const LLGL::Extent2D resolution = swapChain_->GetResolution();
        const float physicalWidth = static_cast<float>(resolution.width);
        const float physicalHeight = static_cast<float>(resolution.height);
        if (physicalWidth <= 0.0f || physicalHeight <= 0.0f)
            return;

        // Column-major orthographic projection from window pixels (Y down, origin top-left) to
        // clip space. Clip space is Y-up on every LLGL module: LLGL submits Vulkan viewports with
        // a negated height, which flips Vulkan's natively Y-down clip space to match OpenGL's.
        // RenderingCapabilities::screenOrigin does NOT describe this -- it describes where the
        // origin of viewport and scissor RECTANGLES sits, which LLGL normalizes to upper-left
        // everywhere -- so keying the sign off it renders the whole scene upside down (found by
        // reading back real pixels: the texture's bottom row appeared at the top of the sprite).
        const float projection[16] = {
            2.0f / physicalWidth, 0.0f,                   0.0f, 0.0f,
            0.0f,                 -2.0f / physicalHeight, 0.0f, 0.0f,
            0.0f,                 0.0f,                   1.0f, 0.0f,
            -1.0f,                1.0f,                   0.0f, 1.0f
        };
        renderer_->WriteBuffer(*spriteProjectionBuffer_, 0, projection, sizeof(projection));

        // One constant buffer per 3D draw recorded this frame. The pool only ever grows, so a
        // steady-state frame allocates nothing.
        const std::size_t transformCount = transformData_.size() / kEffectUniformFloats;
        while (transformBuffers_.size() < transformCount)
        {
            LLGL::BufferDescriptor transformDesc;
            transformDesc.size = sizeof(float) * kEffectUniformFloats;
            transformDesc.bindFlags = LLGL::BindFlags::ConstantBuffer;
            LLGL::Buffer* buffer = renderer_->CreateBuffer(transformDesc);
            if (buffer == nullptr)
                throw std::runtime_error(std::string(kBackendName) + " backend: transform buffer creation failed");
            transformBuffers_.push_back(buffer);
        }
        for (std::size_t index = 0; index < transformCount; ++index)
        {
            renderer_->WriteBuffer(*transformBuffers_[index], 0,
                                   transformData_.data() + index * kEffectUniformFloats,
                                   sizeof(float) * kEffectUniformFloats);
        }

        if (spriteVertexData_.empty())
            return;

        const std::size_t vertexCount = spriteVertexData_.size() / kSpriteVertexFloats;
        const std::size_t byteSize = spriteVertexData_.size() * sizeof(float);

        if (spriteVertexBuffer_ == nullptr || vertexCount > spriteVertexCapacity_)
        {
            if (spriteVertexBuffer_ != nullptr)
                renderer_->Release(*spriteVertexBuffer_);

            const LLGL::VertexFormat vertexFormat = MakeSpriteVertexFormat();
            LLGL::BufferDescriptor bufferDesc;
            bufferDesc.size = byteSize;
            bufferDesc.bindFlags = LLGL::BindFlags::VertexBuffer;
            bufferDesc.vertexAttribs = vertexFormat.attributes;
            spriteVertexBuffer_ = renderer_->CreateBuffer(bufferDesc, spriteVertexData_.data());
            if (spriteVertexBuffer_ == nullptr)
                throw std::runtime_error(std::string(kBackendName) + " backend: sprite vertex buffer creation failed");
            spriteVertexCapacity_ = vertexCount;
        }
        else
        {
            renderer_->WriteBuffer(*spriteVertexBuffer_, 0, spriteVertexData_.data(), byteSize);
        }
    }

    void LlglGraphicsBackend::ScheduleBufferReleaseEXT(LLGL::Buffer* buffer)
    {
        if (buffer == nullptr)
            return;

        // Nothing recorded refers to it, so there is nothing to wait for.
        if (frameCommands_.empty())
        {
            renderer_->Release(*buffer);
            return;
        }

        pendingBufferReleases_.push_back(buffer);
    }

    void LlglGraphicsBackend::ReleasePendingBuffers()
    {
        if (pendingBufferReleases_.empty())
            return;

        // The submitted frame may still be reading these buffers. Waiting only happens on the
        // frames where a resource actually died, which is not a hot path.
        queue_->WaitIdle();
        for (LLGL::Buffer* buffer : pendingBufferReleases_)
        {
            if (buffer != nullptr)
                renderer_->Release(*buffer);
        }
        pendingBufferReleases_.clear();
    }

    void LlglGraphicsBackend::ReplayFrameCommands()
    {
        for (const FrameCommand& command : frameCommands_)
        {
            switch (command.kind)
            {
                case FrameCommand::Kind::Clear:
                {
                    LLGL::ClearValue clearValue;
                    std::memcpy(clearValue.color, command.clearColor, sizeof(clearValue.color));
                    clearValue.depth = command.clearDepth;
                    clearValue.stencil = command.clearStencil;
                    commands_->Clear(command.clearFlags, clearValue);
                    break;
                }

                case FrameCommand::Kind::Primitives:
                {
                    if (command.vertexBuffer == nullptr || command.pipeline == nullptr ||
                        command.transformIndex >= transformBuffers_.size())
                    {
                        break;
                    }

                    commands_->SetPipelineState(*command.pipeline);
                    if (command.usesBlendFactor)
                        commands_->SetBlendFactor(command.blendFactor);
                    if (command.scissorEnabled)
                    {
                        commands_->SetScissor(LLGL::Scissor{command.scissor[0], command.scissor[1],
                                                            command.scissor[2], command.scissor[3]});
                    }
                    commands_->SetResource(0, *transformBuffers_[command.transformIndex]);
                    if (command.texture != nullptr && command.sampler != nullptr)
                    {
                        commands_->SetResource(1, *command.texture);
                        commands_->SetResource(2, *command.sampler);
                    }
                    commands_->SetVertexBuffer(*command.vertexBuffer);
                    if (command.indexBuffer != nullptr)
                    {
                        commands_->SetIndexBuffer(*command.indexBuffer);
                        commands_->DrawIndexed(command.vertexCount, command.firstIndex, command.baseVertex);
                    }
                    else
                    {
                        commands_->Draw(command.vertexCount, command.firstVertex);
                    }
                    break;
                }

                case FrameCommand::Kind::Sprite:
                {
                    if (command.texture == nullptr || command.sampler == nullptr ||
                        command.pipeline == nullptr || spriteVertexBuffer_ == nullptr)
                    {
                        break;
                    }

                    commands_->SetPipelineState(*command.pipeline);
                    if (command.usesBlendFactor)
                        commands_->SetBlendFactor(command.blendFactor);
                    if (command.scissorEnabled)
                    {
                        commands_->SetScissor(LLGL::Scissor{command.scissor[0], command.scissor[1],
                                                            command.scissor[2], command.scissor[3]});
                    }
                    commands_->SetResource(0, *spriteProjectionBuffer_);
                    commands_->SetResource(1, *command.texture);
                    commands_->SetResource(2, *command.sampler);
                    commands_->SetVertexBuffer(*spriteVertexBuffer_);
                    commands_->Draw(command.vertexCount, command.firstVertex);
                    break;
                }
            }
        }
    }

    void LlglGraphicsBackend::RecordAndSubmitFrame()
    {
        const LLGL::Extent2D resolution = swapChain_->GetResolution();

        commands_->Begin();
        commands_->BeginRenderPass(*swapChain_);
        commands_->SetViewport(LLGL::Viewport{0.0f, 0.0f,
                                              static_cast<float>(resolution.width),
                                              static_cast<float>(resolution.height)});
        ReplayFrameCommands();
        commands_->EndRenderPass();
        commands_->End();
        queue_->Submit(*commands_);
    }

    void LlglGraphicsBackend::Present()
    {
        if (swapChain_ == nullptr)
            return;

        UpdateSwapChainResolution();

        // A frame that ReadBackbuffer() already recorded and submitted is not recorded a second
        // time: re-entering the render pass would begin from an undefined attachment and could
        // present something other than what was read back.
        if (!frameCommands_.empty() || !frameSubmitted_)
        {
            UploadFrameResources();
            RecordAndSubmitFrame();
        }

        swapChain_->Present();
        ReleasePendingBuffers();

        frameCommands_.clear();
        spriteVertexData_.clear();
        transformData_.clear();
        frameSubmitted_ = false;
        backbufferCacheValid_ = false;
    }

    void LlglGraphicsBackend::CaptureBackbuffer()
    {
        const LLGL::Extent2D resolution = swapChain_->GetResolution();
        if (resolution.width == 0 || resolution.height == 0)
            throw std::runtime_error(std::string(kBackendName) + " backend: the swap chain has no pixels to read");

        LLGL::TextureDescriptor stagingDesc;
        stagingDesc.type = LLGL::TextureType::Texture2D;
        // CopyDst is what CopyTextureFromFramebuffer writes through and CopySrc is what
        // ReadTexture reads through; without them LLGL rejects the copy and the caller would be
        // handed this texture's zero-initialised contents as if they were the frame.
        stagingDesc.bindFlags = LLGL::BindFlags::CopyDst | LLGL::BindFlags::CopySrc;
        // The staging texture takes the swap chain's OWN colour format. The copy is a raw image
        // transfer with no channel reordering, so a swap chain that presents B8G8R8A8 (which is
        // what the Vulkan module selects here) would hand back byte-swapped pixels through an
        // RGBA8 staging texture. Matching the format makes ReadTexture's conversion to
        // ImageFormat::RGBA the single place where channel order is resolved.
        stagingDesc.format = swapChain_->GetColorFormat();
        stagingDesc.extent = {resolution.width, resolution.height, 1};
        stagingDesc.mipLevels = 1;
        stagingDesc.miscFlags = 0;

        LLGL::Texture* staging = renderer_->CreateTexture(stagingDesc);
        if (staging == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: readback texture creation failed");

        // LLGL allows the framebuffer copy only inside a render pass, and the caller expects to
        // read what THIS frame drew -- so the pending frame is recorded and the copy appended to
        // that very same pass, instead of reading whatever the previous Present() left behind.
        UploadFrameResources();

        commands_->Begin();
        commands_->BeginRenderPass(*swapChain_);
        commands_->SetViewport(LLGL::Viewport{0.0f, 0.0f,
                                              static_cast<float>(resolution.width),
                                              static_cast<float>(resolution.height)});
        ReplayFrameCommands();

        LLGL::TextureRegion region;
        region.subresource.baseMipLevel = 0;
        region.subresource.numMipLevels = 1;
        region.offset = {0, 0, 0};
        region.extent = {resolution.width, resolution.height, 1};
        commands_->CopyTextureFromFramebuffer(*staging, region, LLGL::Offset2D{0, 0});

        commands_->EndRenderPass();
        commands_->End();
        queue_->Submit(*commands_);
        queue_->WaitIdle();
        ReleasePendingBuffers();

        frameCommands_.clear();
        spriteVertexData_.clear();
        transformData_.clear();
        frameSubmitted_ = true;

        backbufferCache_.assign(
            static_cast<std::size_t>(resolution.width) * static_cast<std::size_t>(resolution.height) * 4u, 0);

        LLGL::MutableImageView imageView;
        imageView.format = LLGL::ImageFormat::RGBA;
        imageView.dataType = LLGL::DataType::UInt8;
        imageView.data = backbufferCache_.data();
        imageView.dataSize = backbufferCache_.size();

        renderer_->ReadTexture(*staging, region, imageView);
        renderer_->Release(*staging);

        backbufferCacheWidth_ = static_cast<int>(resolution.width);
        backbufferCacheHeight_ = static_cast<int>(resolution.height);
        backbufferCacheValid_ = true;
    }

    void LlglGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error(std::string(kBackendName) + " backend: invalid ReadBackbuffer region");
        if (swapChain_ == nullptr)
            throw std::runtime_error(std::string(kBackendName) + " backend: no swap chain to read back");

        // The whole back buffer is captured once and every region of the same frame is served from
        // that capture. Re-entering the swap chain's render pass for a second copy is not an
        // option: its colour attachment is loaded with Undefined, so the second pass would begin
        // from discarded content and hand back an image the frame never contained.
        if (!backbufferCacheValid_)
            CaptureBackbuffer();

        // The caller's region is in logical (virtual-resolution) coordinates, which coincide with
        // window pixels only when the presentation is 1:1. Under any letterbox or scale, one
        // logical pixel covers a block of window pixels.
        const PresentationRect rect = ComputePresentationRect();
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (rect.logicalWidth > 0.0f && rect.logicalHeight > 0.0f && rect.width > 0.0f && rect.height > 0.0f)
        {
            offsetX = rect.x;
            offsetY = rect.y;
            scaleX = rect.width / rect.logicalWidth;
            scaleY = rect.height / rect.logicalHeight;
        }

        const bool oneToOne = (offsetX == 0.0f && offsetY == 0.0f && scaleX == 1.0f && scaleY == 1.0f);

        // A scaled logical pixel is resolved to the window pixel at the CENTRE of the block it
        // covers -- nearest-neighbour, deliberately not an average. Every value handed back is
        // then a colour the frame genuinely contained at a known position; averaging would invent
        // colours that were never rendered, which is precisely what a pixel test must not be given.
        const auto sampleWindowX = [&](int logicalX) {
            const float centre = offsetX + (static_cast<float>(logicalX) + 0.5f) * scaleX;
            return std::clamp(static_cast<int>(centre), 0, backbufferCacheWidth_ - 1);
        };
        const auto sampleWindowY = [&](int logicalY) {
            const float centre = offsetY + (static_cast<float>(logicalY) + 0.5f) * scaleY;
            return std::clamp(static_cast<int>(centre), 0, backbufferCacheHeight_ - 1);
        };

        if (backbufferCacheWidth_ <= 0 || backbufferCacheHeight_ <= 0)
            throw std::runtime_error(std::string(kBackendName) + " backend: the captured back buffer is empty");

        if (oneToOne)
        {
            if (x < 0 || y < 0 || x + w > backbufferCacheWidth_ || y + h > backbufferCacheHeight_)
            {
                throw std::runtime_error(
                    std::string(kBackendName) + " backend: ReadBackbuffer region lies outside the back buffer");
            }

            for (int row = 0; row < h; ++row)
            {
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(backbufferCacheWidth_) +
                     static_cast<std::size_t>(x)) * 4u;
                std::memcpy(pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4u,
                            backbufferCache_.data() + sourceOffset,
                            static_cast<std::size_t>(w) * 4u);
            }
            return;
        }

        for (int row = 0; row < h; ++row)
        {
            const int sourceRow = sampleWindowY(y + row);
            for (int column = 0; column < w; ++column)
            {
                const int sourceColumn = sampleWindowX(x + column);
                const std::size_t sourceOffset =
                    (static_cast<std::size_t>(sourceRow) * static_cast<std::size_t>(backbufferCacheWidth_) +
                     static_cast<std::size_t>(sourceColumn)) * 4u;
                const std::size_t destinationOffset =
                    (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(column)) * 4u;
                std::memcpy(pixels + destinationOffset, backbufferCache_.data() + sourceOffset, 4u);
            }
        }
    }

    void LlglGraphicsBackend::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        // Restoring the back buffer is the only binding this backend can honour, and it is already
        // the only thing it ever draws to. Anything else must fail rather than quietly redirect a
        // render-target pass onto the window.
        if (renderTargets == nullptr || count <= 0)
            return;

        NotYetImplemented(kBackendName, "render targets");
    }

    void LlglGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        colorSrcBlend_ = colorSrcBlend;
        alphaSrcBlend_ = alphaSrcBlend;
        colorDstBlend_ = colorDstBlend;
        alphaDstBlend_ = alphaDstBlend;
        colorBlendFunc_ = colorBlendFunc;
        alphaBlendFunc_ = alphaBlendFunc;
        // Only slot 0's write mask is applied: this backend renders to a single attachment, so
        // slots 1..3 have nothing to apply to. BlendState.MultiSampleMask is likewise not applied
        // -- both are documented boundaries of the current 2D scope, not values lost in silence.
        colorWriteChannels_ = writeState.colorWriteChannels[0];
    }

    void LlglGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void LlglGraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                 int maxAnisotropy)
    {
        // Slot 0 is the only unit the sprite pipeline samples; a state applied to any other slot
        // has no shader binding to reach on this backend yet.
        if (slot != 0)
            return;

        samplerFilter_ = filter;
        samplerAddressU_ = addressU;
        samplerAddressV_ = addressV;
        samplerMaxAnisotropy_ = maxAnisotropy;
    }

    void LlglGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                    float /*depthBias*/, float /*slopeScaleDepthBias*/)
    {
        // Cull mode, fill mode and both depth biases belong to the 3D pipeline: a sprite quad is
        // always front-facing, solid, and unaffected by depth. They are recorded so the 3D path
        // can consume them when it lands.
        cullMode_ = cullMode;
        fillMode_ = fillMode;
        scissorTestEnabled_ = scissorTestEnable;
    }

    void LlglGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        scissorRectSet_ = true;
    }

    void LlglGraphicsBackend::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
    {
        viewportRect_[0] = x;
        viewportRect_[1] = y;
        viewportRect_[2] = w;
        viewportRect_[3] = h;

        const PresentationRect rect = ComputePresentationRect();
        viewportSet_ = !(x == 0 && y == 0 &&
                         static_cast<float>(w) == rect.logicalWidth &&
                         static_cast<float>(h) == rect.logicalHeight);
    }

    void LlglGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                      int depthFunc,
                                                      bool stencilEnable, int /*stencilFunc*/,
                                                      int /*stencilPass*/, int /*stencilFail*/,
                                                      int /*stencilDepthFail*/,
                                                      int /*stencilMask*/, int /*stencilWriteMask*/,
                                                      int /*referenceStencil*/,
                                                      bool /*twoSidedStencilMode*/,
                                                      int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                      int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        depthTestEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthCompareFunction_ = depthFunc;
        // Stencil is deliberately not applied: the swap chain carries a real stencil buffer and
        // ClearStencil works, but no draw path consumes a stencil test yet, so translating the
        // eight stencil fields into a pipeline nothing reads would only look like support.
        stencilRequested_ = stencilEnable;
    }

    void LlglGraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        depthTestEnabled_ = enabled;
    }

    void LlglGraphicsBackend::SetBlendEnabled(bool enabled)
    {
        // Recorded only: the sprite pipeline derives its blend-enable bit from the blend factors
        // themselves (see AcquireSpritePipeline), which cannot disagree with the applied state.
        blendEnabled_ = enabled;
    }

    void LlglGraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        depthWriteEnabled_ = enabled;
    }

    void LlglGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                     const Matrix& world, const Matrix& view,
                                                     const Matrix& projection,
                                                     PrimitiveType primitive, int primitiveCount)
    {
        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferBackend*>(&vb);
        if (vertexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: the vertex buffer belongs to another backend");
        }

        QueuePrimitives(*vertexBuffer, nullptr, world, view, projection, primitive, primitiveCount,
                        0, 0, 0, nullptr);
    }

    void LlglGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                            const IIndexBufferBackend& ib,
                                                            const Matrix& world, const Matrix& view,
                                                            const Matrix& projection,
                                                            PrimitiveType primitive, int primitiveCount)
    {
        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferBackend*>(&vb);
        const auto* indexBuffer = dynamic_cast<const LlglIndexBufferBackend*>(&ib);
        if (vertexBuffer == nullptr || indexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: the vertex or index buffer belongs to another backend");
        }

        QueuePrimitives(*vertexBuffer, indexBuffer, world, view, projection, primitive, primitiveCount,
                        0, 0, 0, nullptr);
    }

    void LlglGraphicsBackend::RejectUnsupportedDrawParams(const GpuDrawParams& params) const
    {
        // What this path can honour: vertex colours, a diffuse colour and alpha, one texture, fog,
        // and the alpha test. Everything else fails by name rather than quietly rendering
        // something that merely looks plausible -- an unlit surface where lighting was asked for is
        // a wrong answer, not a degraded one.
        const char* unsupported = nullptr;
        if (params.lightingEnabled)                          unsupported = "lighting";
        else if (params.dualTexture)                         unsupported = "DualTextureEffect";
        else if (params.envMapping)                          unsupported = "EnvironmentMapEffect";
        else if (params.skinned)                             unsupported = "SkinnedEffect";
        else if (params.pbr)                                 unsupported = "PbrEffect";
        else if (params.customEffectBackend != nullptr)      unsupported = "custom ShaderEffect";
        else if (params.instanceCount > 1)                   unsupported = "instanced drawing";

        if (unsupported != nullptr)
            NotYetImplemented(kBackendName, unsupported);
    }

    void LlglGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                const Matrix& world, const Matrix& view,
                                                const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams& params)
    {
        RejectUnsupportedDrawParams(params);

        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferBackend*>(&vb);
        if (vertexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: the vertex buffer belongs to another backend");
        }

        QueuePrimitives(*vertexBuffer, nullptr, world, view, projection, primitive, primitiveCount,
                        params.vertexStart, 0, 0, &params);
    }

    void LlglGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                                       const IIndexBufferBackend& ib,
                                                       const Matrix& world, const Matrix& view,
                                                       const Matrix& projection,
                                                       PrimitiveType primitive, int primitiveCount,
                                                       const GpuDrawParams& params)
    {
        RejectUnsupportedDrawParams(params);

        const auto* vertexBuffer = dynamic_cast<const LlglVertexBufferBackend*>(&vb);
        const auto* indexBuffer = dynamic_cast<const LlglIndexBufferBackend*>(&ib);
        if (vertexBuffer == nullptr || indexBuffer == nullptr)
        {
            throw std::runtime_error(
                std::string(kBackendName) + " backend: the vertex or index buffer belongs to another backend");
        }

        // startIndex and baseVertex are honoured rather than dropped: the shared interface's own
        // default implementation forwards neither, which silently turns a sub-range draw into a
        // draw of the wrong range (a defect other backends in this project have had filed against
        // them by name).
        QueuePrimitives(*vertexBuffer, indexBuffer, world, view, projection, primitive, primitiveCount,
                        0, params.startIndex, params.baseVertex, &params);
    }

    bool LlglGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::DepthStencilBuffer:
                return swapChain_ != nullptr &&
                       swapChain_->HasDepthAttachment() && swapChain_->HasStencilAttachment();

            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return true;

            case CNA::GraphicsCapability::AnisotropicFiltering:
                return static_cast<bool>(renderer_) &&
                       renderer_->GetRenderingCaps().limits.maxAnisotropy > 1;

            // The colour-only 3D path is real: vertex and index buffers draw, with depth test,
            // depth write, cull mode and fill mode all applied (LLGL-24). The stock effect family
            // is not implemented yet, which is LLGL-25's scope, not this capability's.
            case CNA::GraphicsCapability::ThreeD:
                return true;

            case CNA::GraphicsCapability::WireFrame:
                return SupportsWireFrameEXT();

            // Everything below is still unimplemented. Reporting false is what lets a caller ask
            // instead of discovering the gap through an exception.
            case CNA::GraphicsCapability::MultipleRenderTargets:
            case CNA::GraphicsCapability::OcclusionQuery:
            case CNA::GraphicsCapability::CustomEffects:
            case CNA::GraphicsCapability::Texture3D:
                return false;
        }
        return false;
    }

    int LlglGraphicsBackend::GetMaxTextureDimension() const
    {
        if (!renderer_)
            return IGraphicsBackend::GetMaxTextureDimension();

        const std::uint32_t maxSize = renderer_->GetRenderingCaps().limits.max2DTextureSize;
        return maxSize > 0 ? static_cast<int>(maxSize) : IGraphicsBackend::GetMaxTextureDimension();
    }
}

#ifdef CNA_BACKEND_LLGL
namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<CNA::Internal::Backends::Llgl::LlglGraphicsBackend>(args);
    }
}
#endif
