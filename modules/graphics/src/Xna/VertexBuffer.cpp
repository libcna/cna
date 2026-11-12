// SPDX-License-Identifier: MS-PL
#include "../Internal/ProfileValidation.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "../Internal/DeferredResourceCreation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "CNA/Graphics/VertexTypeRegistryEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#if CNA_DIAGNOSTICS_LEVEL >= 1
#include "CNA/Internal/Graphics/DiagnosticResource.hpp"
#endif
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics
{
    using CNA::Internal::Graphics::ProfileDiagnostic;
    using CNA::Internal::Graphics::ProfileFormatName;
    namespace
    {
        std::size_t CheckedByteCount(int elementCount,
                                     std::size_t elementSize,
                                     const char* parameterName)
        {
            if (elementCount < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(elementCount),
                    "The element count must be non-negative.");
            }
            const auto unsignedCount = static_cast<std::size_t>(elementCount);
            if (elementSize != 0 &&
                unsignedCount > std::numeric_limits<std::size_t>::max() / elementSize)
            {
                throw System::ArgumentOutOfRangeException(
                    parameterName, std::to_string(elementCount),
                    "The requested byte range is too large.");
            }
            return unsignedCount * elementSize;
        }

        std::size_t CheckedByteOffset(int startIndex, std::size_t elementSize)
        {
            if (startIndex < 0)
            {
                throw System::ArgumentOutOfRangeException(
                    "dataIndex", "This parameter must be a valid index within the array.");
            }
            const auto unsignedStart = static_cast<std::size_t>(startIndex);
            if (elementSize != 0 &&
                unsignedStart > std::numeric_limits<std::size_t>::max() / elementSize)
            {
                throw System::ArgumentOutOfRangeException(
                    "dataIndex", std::to_string(startIndex),
                    "The requested byte offset is too large.");
            }
            return unsignedStart * elementSize;
        }

        // Refresh Helpers.ValidateCopyParameters IL_0000-0041 uses MustBeValidIndex,
        // with no ActualValue. CopyData uses the corresponding FrameworkResources messages.
        void ValidatePositiveElementCount(int elementCount)
        {
            if (elementCount <= 0)
                throw System::ArgumentOutOfRangeException(
                    "elementCount", "This parameter must be a valid index within the array.");
        }

        constexpr SetDataOptions CanonicalizeSetDataOptions(SetDataOptions options) noexcept
        {
            const int bits = static_cast<int>(options);
            if ((bits & static_cast<int>(SetDataOptions::Discard)) != 0)
                return SetDataOptions::Discard;
            if ((bits & static_cast<int>(SetDataOptions::NoOverwrite)) != 0)
                return SetDataOptions::NoOverwrite;
            return SetDataOptions::None;
        }

        // XNA VertexBuffer.CopyData IL_000d-0010 calls Helpers.CheckDisposed(this, ...), which
        // throws ObjectDisposedException(obj.GetType().Name) (Microsoft.Xna.Framework.dll
        // IL_000d-0018): a disposed DynamicVertexBuffer is named "DynamicVertexBuffer".
        [[nodiscard]] std::string DisposedObjectName(const VertexBuffer& buffer)
        {
            const std::string& qualified = buffer.GetTypeName();
            const std::size_t dot = qualified.rfind('.');
            return dot == std::string::npos ? qualified : qualified.substr(dot + 1);
        }

        void ValidateClassicGetDataArguments(const VertexBuffer& buffer,
                                             bool writeOnly,
                                             const void* data,
                                             int startIndex,
                                             int elementCount,
                                             std::size_t elementSize)
        {
            if (buffer.getIsDisposedProperty())
                throw System::ObjectDisposedException(DisposedObjectName(buffer));
            if (data == nullptr)
                throw System::ArgumentNullException(
                    "data", "This method does not accept null for this parameter.");
            if (writeOnly)
                throw System::NotSupportedException(
                    "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
            (void) CheckedByteOffset(startIndex, elementSize);
            ValidatePositiveElementCount(elementCount);
        }

        std::size_t VertexElementSize(VertexElementFormat format)
        {
            switch (format)
            {
                case VertexElementFormat::Single: return 4;
                case VertexElementFormat::Vector2: return 8;
                case VertexElementFormat::Vector3: return 12;
                case VertexElementFormat::Vector4: return 16;
                case VertexElementFormat::Color:
                case VertexElementFormat::Byte4:
                case VertexElementFormat::Short2:
                case VertexElementFormat::NormalizedShort2:
                case VertexElementFormat::HalfVector2:
                    return 4;
                case VertexElementFormat::Short4:
                case VertexElementFormat::NormalizedShort4:
                case VertexElementFormat::HalfVector4:
                    return 8;
            }
            return 0;
        }
    }

    // The shared five-argument constructor registers with the device but defers
    // GraphicsDevice.ResourceCreated; every public constructor announces the finished buffer as
    // its last statement (see Internal/DeferredResourceCreation.hpp).
    VertexBuffer::VertexBuffer(GraphicsDevice& device, int vertexCount)
        : VertexBuffer(device, VertexDeclaration{}, vertexCount, BufferUsage::None, false)
    {
        CNA::Internal::Graphics::PublishResourceCreated(*this);
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& vertexDeclaration,
                               int vertexCount,
                               BufferUsage bufferUsage)
        : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage, false)
    {
        CNA::Internal::Graphics::PublishResourceCreated(*this);
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const System::Type& vertexType,
                               int vertexCount,
                               BufferUsage bufferUsage)
        : VertexBuffer(device, CNA::Graphics::VertexTypeRegistryEXT::Resolve(vertexType),
                       vertexCount, bufferUsage, false)
    {
        CNA::Internal::Graphics::PublishResourceCreated(*this);
    }

    DynamicVertexBuffer::DynamicVertexBuffer(GraphicsDevice& device,
                                             const VertexDeclaration& vertexDeclaration,
                                             int vertexCount,
                                             BufferUsage bufferUsage)
        : VertexBuffer(device, vertexDeclaration, vertexCount, bufferUsage, true)
    {
        CNA::Internal::Graphics::PublishResourceCreated(*this);
    }

    DynamicVertexBuffer::DynamicVertexBuffer(GraphicsDevice& device,
                                             const System::Type& vertexType,
                                             int vertexCount,
                                             BufferUsage bufferUsage)
        : VertexBuffer(device, CNA::Graphics::VertexTypeRegistryEXT::Resolve(vertexType),
                       vertexCount, bufferUsage, true)
    {
        CNA::Internal::Graphics::PublishResourceCreated(*this);
    }

    VertexBuffer::VertexBuffer(GraphicsDevice& device,
                               const VertexDeclaration& vertexDeclaration,
                               int vertexCount,
                               BufferUsage bufferUsage,
                               bool /*dynamic*/)
        : GraphicsResource(CNA::Internal::Graphics::DeferResourceCreated(&device))
        , renderer_(CreateRenderer(device, vertexDeclaration, vertexCount))
        , vertexDeclaration_(vertexDeclaration)
        // Refresh ConvertXnaBufferUsageToDx IL_0000-0009 and
        // ConvertDxBufferUsageToXna IL_0000-0008 retain only the WriteOnly bit.
        , bufferUsage_((static_cast<int>(bufferUsage) & 1) != 0
                           ? BufferUsage::WriteOnly : BufferUsage::None)
        , vertexCount_(vertexCount)
    {
        const auto contextLease = LeaseRendererContext();
        vertexDeclaration_.BindToDevice(device);
        const int stride = vertexDeclaration_.getVertexStrideProperty();
        if (stride > 0)
        {
            const std::size_t capacity =
                CheckedByteCount(vertexCount_, static_cast<std::size_t>(stride), "vertexCount");
            cpuShadow_.resize(capacity, 0U);
            renderer_->SetVertexDeclaration(vertexDeclaration_);
            renderer_->SetData(cpuShadow_.data(), vertexCount_, static_cast<std::size_t>(stride));
        }
#if CNA_DIAGNOSTICS_LEVEL >= 1
        UpdateDiagnosticResourceEXT(CNA::Internal::Graphics::MakeBufferDiagnosticDescriptor(
            CNA::Diagnostics::ResourceKind::VertexBuffer,
            static_cast<std::uint64_t>(std::max(stride, 0)) *
                static_cast<std::uint64_t>(std::max(vertexCount_, 0)),
            "VertexBuffer"));
#endif
    }

    std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer>
    VertexBuffer::CreateRenderer(GraphicsDevice& device,
                                 const VertexDeclaration& vertexDeclaration,
                                 int vertexCount)
    {
        const auto contextLease = LeaseRendererContext(&device);
        // Refresh VB/DVB .ctor uses the two-string overload, without ActualValue.
        if (vertexCount <= 0)
        {
            throw System::ArgumentOutOfRangeException(
                "vertexCount", "Resource size must be greater than zero.");
        }
        vertexDeclaration.ValidateForProfile(device.getGraphicsProfileProperty());

        constexpr std::int64_t maximumBufferBytes = 67'108'863;
        const std::int64_t byteCount =
            static_cast<std::int64_t>(vertexDeclaration.getVertexStrideProperty()) * vertexCount;
        if (byteCount > maximumBufferBytes)
        {
            throw System::NotSupportedException(
                ProfileDiagnostic(device.getGraphicsProfileProperty(),
                    "supports a maximum VertexBuffer size of 67108863."));
        }
        return device.GetRenderer().CreateVertexBuffer(vertexCount);
    }

    // Refresh DynamicVertexBuffer.get_IsContentLost IL_0000-001f latches the device
    // loss without raising an event. GraphicsDevice.get_IsDeviceLost IL_0000-000b
    // also treats a released native device as lost. FNA checks only IsDisposed,
    // without latching the result or detecting Lost/NotReset while the device is alive.
    bool DynamicVertexBuffer::getIsContentLostProperty() const
    {
        // Written only when it turns true, through an atomic reference, so concurrent reads of a
        // target whose device stays Normal never race (a latching write of false did).
        std::atomic_ref<bool> latch(contentLost_);
        if (!latch.load(std::memory_order_relaxed))
        {
            const auto* device = getGraphicsDeviceProperty();
            if (graphicsDeviceLifetime_.expired() || device == nullptr ||
                device->getIsDisposedProperty() ||
                device->getGraphicsDeviceStatusProperty() != GraphicsDeviceStatus::Normal)
                latch.store(true, std::memory_order_relaxed);
        }
        return latch.load(std::memory_order_relaxed);
    }

    VertexBuffer::~VertexBuffer()
    {
        const auto contextLease = TryLeaseRendererContext();
        if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
            graphicsDevice_->DetachDestroyedVertexBuffer(this);
        Dispose(false);
    }
    VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept
        : GraphicsResource(std::move(other))
        , renderer_(std::move(other.renderer_))
        , vertexDeclaration_(std::move(other.vertexDeclaration_))
        , bufferUsage_(other.bufferUsage_)
        , vertexCount_(other.vertexCount_)
        , cpuShadow_(std::move(other.cpuShadow_))
    {
        if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
            graphicsDevice_->TransferMovedVertexBuffer(&other, this);
        other.vertexCount_ = 0;
    }

    VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept
    {
        if (this != &other)
        {
            GraphicsDevice* const previousDevice = graphicsDevice_;
            const bool previousDeviceAlive = !graphicsDeviceLifetime_.expired();
            if (previousDevice != nullptr && previousDeviceAlive &&
                previousDevice != other.graphicsDevice_)
            {
                previousDevice->DetachDestroyedVertexBuffer(this);
            }
            GraphicsResource::operator=(std::move(other));
            renderer_ = std::move(other.renderer_);
            vertexDeclaration_ = std::move(other.vertexDeclaration_);
            bufferUsage_ = other.bufferUsage_;
            vertexCount_ = other.vertexCount_;
            cpuShadow_ = std::move(other.cpuShadow_);
            if (graphicsDevice_ != nullptr && !graphicsDeviceLifetime_.expired())
                graphicsDevice_->TransferMovedVertexBuffer(&other, this);
            other.vertexCount_ = 0;
        }
        return *this;
    }

    void VertexBuffer::Dispose(bool disposing)
    {
        const auto contextLease = TryLeaseRendererContext();
        renderer_.reset();
        GraphicsResource::Dispose(disposing);
    }

    GetTypeNameCPP(VertexBuffer, "Microsoft.Xna.Framework.Graphics.VertexBuffer")

    bool VertexBuffer::ValidateSetDataRange(const void* data,
                                            int startIndex,
                                            int elementCount,
                                            std::size_t sourceElementSize,
                                            std::size_t uploadStride,
                                            bool rawUpload,
                                            SetDataOptions options,
                                            bool useOptions,
                                            int vertexStride) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException(DisposedObjectName(*this));

        if (!rawUpload)
        {
            if (data == nullptr)
                throw System::ArgumentNullException(
                    "data", "This method does not accept null for this parameter.");
            ThrowIfSetDataResourceInUse(options, useOptions);
        }

        (void) CheckedByteOffset(startIndex, sourceElementSize);
        if (!rawUpload)
            ValidatePositiveElementCount(elementCount);
        (void) CheckedByteCount(elementCount, sourceElementSize, "elementCount");
        // Microsoft XNA VertexBuffer::CopyData IL_00b7-00d6 refuses a non-zero vertexStride
        // smaller than the element before IL_00eb-0100 compares the transfer with the buffer
        // size, so this must precede the whole-buffer capacity check below.
        if (vertexStride != 0 &&
            (vertexStride < 0 || static_cast<std::size_t>(vertexStride) < uploadStride))
        {
            throw System::ArgumentOutOfRangeException(
                "vertexStride",
                "The vertex stride is too small for the type of data requested. This is not allowed.");
        }
        const std::size_t uploadByteCount =
            CheckedByteCount(elementCount, uploadStride, "elementCount");

        if (uploadStride == 0 ||
            uploadStride > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(uploadStride),
                "The vertex stride must be a positive Int32 byte count.");
        }

        const auto& declarationElements = vertexDeclaration_.GetVertexElements();
        if (rawUpload && !declarationElements.empty())
        {
            if (vertexDeclaration_.getVertexStrideProperty() !=
                static_cast<int>(uploadStride))
            {
                throw System::ArgumentException(
                    "The raw upload stride must match the VertexBuffer's VertexDeclaration.",
                    "stride");
            }

            for (const VertexElement& element : declarationElements)
            {
                const int offset = element.getOffsetProperty();
                const std::size_t elementSize =
                    VertexElementSize(element.getVertexElementFormatProperty());
                if (offset < 0 || elementSize == 0 ||
                    static_cast<std::size_t>(offset) > uploadStride ||
                    elementSize > uploadStride - static_cast<std::size_t>(offset))
                {
                    throw System::ArgumentException(
                        "The VertexDeclaration contains an element outside the uploaded vertex stride.",
                        "data");
                }
            }
        }

        // The whole-buffer typed paths validated here have destination offset zero. Windowed
        // transfers perform their additional byte-span validation in SetDataElementsAtInternal.
        if (elementCount == 0)
            return false;
        if (data == nullptr)
            throw System::ArgumentNullException(
                "data", "This method does not accept null for this parameter.");
        const int declarationStride = vertexDeclaration_.getVertexStrideProperty();
        const std::size_t bufferStride =
            declarationStride > 0 ? static_cast<std::size_t>(declarationStride) : uploadStride;
        const std::size_t bufferCapacity =
            CheckedByteCount(vertexCount_, bufferStride, "vertexCount");
        if (uploadByteCount > bufferCapacity)
            throw System::InvalidOperationException(
                "The array is not the correct size for the amount of data requested.");
        return true;
    }

    void VertexBuffer::UploadValidatedData(const void* data,
                                           int elementCount,
                                           std::size_t uploadStride,
                                           SetDataOptions options,
                                           bool useOptions)
    {
        const auto contextLease = LeaseRendererContext();
        options = CanonicalizeSetDataOptions(options);
        ThrowIfSetDataResourceInUse(options, useOptions);
        const int declarationStride = vertexDeclaration_.getVertexStrideProperty();
        const std::size_t bufferStride =
            declarationStride > 0 ? static_cast<std::size_t>(declarationStride) : uploadStride;
        const std::size_t capacity =
            CheckedByteCount(vertexCount_, bufferStride, "vertexCount");
        const std::size_t byteCount =
            CheckedByteCount(elementCount, uploadStride, "elementCount");

        // XNA copies a contiguous byte span into fixed-size buffer storage; sizeof(T) controls
        // that span but never changes the declaration stride used for drawing. The renderer
        // contract has no prefix-update operation, so retain the remainder in the CPU shadow and
        // submit the complete logical buffer. Discard makes the unnamed remainder undefined; zero
        // is CNA's deterministic representation of that state.
        if (cpuShadow_.size() != capacity)
            cpuShadow_.resize(capacity, 0U);
        if (useOptions && options == SetDataOptions::Discard)
            std::fill(cpuShadow_.begin(), cpuShadow_.end(), 0U);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::copy(bytes, bytes + byteCount, cpuShadow_.begin());

        // GFX-043: propagate this buffer's complete declaration before every real upload. The
        // shared empty branch returns before this method, so even declaration propagation is not
        // a renderer call for an empty operation.
        // CABI-15: content written again, so it is no longer lost. Doing it on this single
        // upload path rather than in the 13 SetData overloads is what keeps the two in step.
        if (auto* const losable = dynamic_cast<CNA::Internal::Graphics::IContentLosable*>(this)) {
            losable->ClearContentLostEXT();
        }
        renderer_->SetVertexDeclaration(vertexDeclaration_);
        if (useOptions)
            renderer_->SetDataWithOptions(
                cpuShadow_.data(), vertexCount_, bufferStride, options);
        else
            renderer_->SetData(cpuShadow_.data(), vertexCount_, bufferStride);
    }

    // Refresh VertexBuffer.CopyData IL_002c-0081 maps a bound write through
    // Framework.Helpers.GetExceptionFromResult IL_0247-0251 (ResourceInUse).
    void VertexBuffer::ThrowIfSetDataResourceInUse(SetDataOptions options,
                                                   bool useOptions) const
    {
        options = CanonicalizeSetDataOptions(options);
        if (useOptions &&
            (options == SetDataOptions::Discard || options == SetDataOptions::NoOverwrite))
        {
            return;
        }
        GraphicsDevice* const device = getGraphicsDeviceProperty();
        if (device == nullptr)
            return;
        for (const VertexBufferBinding& binding : device->GetVertexBuffers())
        {
            if (binding.getVertexBufferProperty() == this)
                throw System::InvalidOperationException("You may not call SetData on a resource while it is actively set on the GraphicsDevice. Unset it from the device before calling SetData.");
        }
    }

    void VertexBuffer::SetData(const VertexPositionColor* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionColor* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionColor* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        SetDataAtInternal(
            0, data, startIndex, elementCount, 0, options, useOptions);
    }

    void VertexBuffer::SetDataAtInternal(const int offsetInBytes,
                                         const VertexPositionColor* data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride,
                                         SetDataOptions options,
                                         const bool useOptions)
    {
        // The C++ object is not the GPU stream: Color inherits a polymorphic IPackedVector
        // base, so a VertexPositionColor carries a vtable pointer and alignment padding that
        // never reach a renderer. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionColorStream;

        if (!ValidateSetDataRange(data, startIndex, elementCount,
                                  sizeof(VertexPositionColor), sizeof(GpuVertex), false,
                                  options, useOptions, vertexStride))
        {
            if (offsetInBytes != 0 || vertexStride != 0)
                SetDataElementsAtInternal(offsetInBytes, nullptr, 0, 0, sizeof(GpuVertex),
                                          vertexStride, options, useOptions);
            return;
        }

        const VertexPositionColor* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x = source[i].Position.X;
            packed[i].y = source[i].Position.Y;
            packed[i].z = source[i].Position.Z;
            packed[i].r = source[i].Color.getRProperty();
            packed[i].g = source[i].Color.getGProperty();
            packed[i].b = source[i].Color.getBProperty();
            packed[i].a = source[i].Color.getAProperty();
        }
        const int resolvedStride = vertexStride == 0
                                       ? static_cast<int>(sizeof(GpuVertex))
                                       : vertexStride;
        if (offsetInBytes == 0 && resolvedStride == static_cast<int>(sizeof(GpuVertex)))
            UploadValidatedData(
                packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
        else
            SetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                      sizeof(GpuVertex), resolvedStride, options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionColor* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionColor* data, int startIndex, int elementCount)
    {
        GetDataAtInternal(0, data, startIndex, elementCount, 0);
    }

    void VertexBuffer::GetDataAtInternal(const int offsetInBytes,
                                         VertexPositionColor* const data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride) const
    {
        using GpuVertex = CNA::Internal::Graphics::PositionColorStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                  sizeof(GpuVertex), vertexStride);
        for (int i = 0; i < elementCount; ++i)
        {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] =
                VertexPositionColor(Vector3(v.x, v.y, v.z), Color(v.r, v.g, v.b, v.a));
        }
    }

    void VertexBuffer::SetData(const VertexPositionColorTexture* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionColorTexture* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionColorTexture* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        SetDataAtInternal(
            0, data, startIndex, elementCount, 0, options, useOptions);
    }

    void VertexBuffer::SetDataAtInternal(const int offsetInBytes,
                                         const VertexPositionColorTexture* data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride,
                                         SetDataOptions options,
                                         const bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionColorTextureStream;

        if (!ValidateSetDataRange(data, startIndex, elementCount,
                                  sizeof(VertexPositionColorTexture), sizeof(GpuVertex), false,
                                  options, useOptions, vertexStride))
        {
            if (offsetInBytes != 0 || vertexStride != 0)
                SetDataElementsAtInternal(offsetInBytes, nullptr, 0, 0, sizeof(GpuVertex),
                                          vertexStride, options, useOptions);
            return;
        }

        const VertexPositionColorTexture* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x = source[i].Position.X;
            packed[i].y = source[i].Position.Y;
            packed[i].z = source[i].Position.Z;
            packed[i].r = source[i].Color.getRProperty();
            packed[i].g = source[i].Color.getGProperty();
            packed[i].b = source[i].Color.getBProperty();
            packed[i].a = source[i].Color.getAProperty();
            packed[i].u = source[i].TextureCoordinate.X;
            packed[i].v = source[i].TextureCoordinate.Y;
        }
        const int resolvedStride = vertexStride == 0
                                       ? static_cast<int>(sizeof(GpuVertex))
                                       : vertexStride;
        if (offsetInBytes == 0 && resolvedStride == static_cast<int>(sizeof(GpuVertex)))
            UploadValidatedData(
                packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
        else
            SetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                      sizeof(GpuVertex), resolvedStride, options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionColorTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionColorTexture* data, int startIndex, int elementCount)
    {
        GetDataAtInternal(0, data, startIndex, elementCount, 0);
    }

    void VertexBuffer::GetDataAtInternal(const int offsetInBytes,
                                         VertexPositionColorTexture* const data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride) const
    {
        using GpuVertex = CNA::Internal::Graphics::PositionColorTextureStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                  sizeof(GpuVertex), vertexStride);
        for (int i = 0; i < elementCount; ++i)
        {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] = VertexPositionColorTexture(
                Vector3(v.x, v.y, v.z), Color(v.r, v.g, v.b, v.a), Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTexture* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionNormalTexture* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        SetDataAtInternal(
            0, data, startIndex, elementCount, 0, options, useOptions);
    }

    void VertexBuffer::SetDataAtInternal(const int offsetInBytes,
                                         const VertexPositionNormalTexture* data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride,
                                         SetDataOptions options,
                                         const bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureStream;

        if (!ValidateSetDataRange(data, startIndex, elementCount,
                                  sizeof(VertexPositionNormalTexture), sizeof(GpuVertex), false,
                                  options, useOptions, vertexStride))
        {
            if (offsetInBytes != 0 || vertexStride != 0)
                SetDataElementsAtInternal(offsetInBytes, nullptr, 0, 0, sizeof(GpuVertex),
                                          vertexStride, options, useOptions);
            return;
        }

        const VertexPositionNormalTexture* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x  = source[i].Position.X;
            packed[i].y  = source[i].Position.Y;
            packed[i].z  = source[i].Position.Z;
            packed[i].nx = source[i].Normal.X;
            packed[i].ny = source[i].Normal.Y;
            packed[i].nz = source[i].Normal.Z;
            packed[i].u  = source[i].TextureCoordinate.X;
            packed[i].v  = source[i].TextureCoordinate.Y;
        }
        const int resolvedStride = vertexStride == 0
                                       ? static_cast<int>(sizeof(GpuVertex))
                                       : vertexStride;
        if (offsetInBytes == 0 && resolvedStride == static_cast<int>(sizeof(GpuVertex)))
            UploadValidatedData(
                packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
        else
            SetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                      sizeof(GpuVertex), resolvedStride, options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionNormalTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTexture* data, int startIndex, int elementCount)
    {
        GetDataAtInternal(0, data, startIndex, elementCount, 0);
    }

    void VertexBuffer::GetDataAtInternal(const int offsetInBytes,
                                         VertexPositionNormalTexture* const data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride) const
    {
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                  sizeof(GpuVertex), vertexStride);
        for (int i = 0; i < elementCount; ++i)
        {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] = VertexPositionNormalTexture(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz), Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionTexture* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionTexture* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(const VertexPositionTexture* data,
                                       int startIndex,
                                       int elementCount,
                                       SetDataOptions options,
                                       bool useOptions)
    {
        SetDataAtInternal(
            0, data, startIndex, elementCount, 0, options, useOptions);
    }

    void VertexBuffer::SetDataAtInternal(const int offsetInBytes,
                                         const VertexPositionTexture* data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride,
                                         SetDataOptions options,
                                         const bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionTextureStream;

        if (!ValidateSetDataRange(data, startIndex, elementCount,
                                  sizeof(VertexPositionTexture), sizeof(GpuVertex), false,
                                  options, useOptions, vertexStride))
        {
            if (offsetInBytes != 0 || vertexStride != 0)
                SetDataElementsAtInternal(offsetInBytes, nullptr, 0, 0, sizeof(GpuVertex),
                                          vertexStride, options, useOptions);
            return;
        }

        const VertexPositionTexture* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x = source[i].Position.X;
            packed[i].y = source[i].Position.Y;
            packed[i].z = source[i].Position.Z;
            packed[i].u = source[i].TextureCoordinate.X;
            packed[i].v = source[i].TextureCoordinate.Y;
        }
        const int resolvedStride = vertexStride == 0
                                       ? static_cast<int>(sizeof(GpuVertex))
                                       : vertexStride;
        if (offsetInBytes == 0 && resolvedStride == static_cast<int>(sizeof(GpuVertex)))
            UploadValidatedData(
                packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
        else
            SetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                      sizeof(GpuVertex), resolvedStride, options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionTexture* data, int startIndex, int elementCount)
    {
        GetDataAtInternal(0, data, startIndex, elementCount, 0);
    }

    void VertexBuffer::GetDataAtInternal(const int offsetInBytes,
                                         VertexPositionTexture* const data,
                                         const int startIndex,
                                         const int elementCount,
                                         const int vertexStride) const
    {
        using GpuVertex = CNA::Internal::Graphics::PositionTextureStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(offsetInBytes, packed.data(), 0, elementCount,
                                  sizeof(GpuVertex), vertexStride);
        for (int i = 0; i < elementCount; ++i)
        {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] =
                VertexPositionTexture(Vector3(v.x, v.y, v.z), Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTextureSkinned* data, int count)
    {
        SetDataInternal(data, 0, count, SetDataOptions::None, false);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTextureSkinned* data,
                               int startIndex,
                               int elementCount)
    {
        SetDataInternal(
            data, startIndex, elementCount, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataInternal(
        const VertexPositionNormalTextureSkinned* data,
        int startIndex,
        int elementCount,
        SetDataOptions options,
        bool useOptions)
    {
        // The IVertexType base is polymorphic here, so the object carries a vtable pointer the
        // stream must not. Pack into the stream this type's VertexDeclaration describes.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureSkinnedStream;

        if (!ValidateSetDataRange(data,
                                  startIndex,
                                  elementCount,
                                  sizeof(VertexPositionNormalTextureSkinned),
                                  sizeof(GpuVertex),
                                  false,
                                  options,
                                  useOptions))
        {
            return;
        }

        const VertexPositionNormalTextureSkinned* source = data + startIndex;
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        for (int i = 0; i < elementCount; ++i) {
            packed[i].x  = source[i].Position.X;
            packed[i].y  = source[i].Position.Y;
            packed[i].z  = source[i].Position.Z;
            packed[i].nx = source[i].Normal.X;
            packed[i].ny = source[i].Normal.Y;
            packed[i].nz = source[i].Normal.Z;
            packed[i].u  = source[i].TextureCoordinate.X;
            packed[i].v  = source[i].TextureCoordinate.Y;
            packed[i].w0 = source[i].BlendWeight.X;
            packed[i].w1 = source[i].BlendWeight.Y;
            packed[i].w2 = source[i].BlendWeight.Z;
            packed[i].w3 = source[i].BlendWeight.W;
            packed[i].i0 = source[i].BlendIndices[0];
            packed[i].i1 = source[i].BlendIndices[1];
            packed[i].i2 = source[i].BlendIndices[2];
            packed[i].i3 = source[i].BlendIndices[3];
        }
        UploadValidatedData(
            packed.data(), elementCount, sizeof(GpuVertex), options, useOptions);
    }

    void VertexBuffer::GetData(VertexPositionNormalTextureSkinned* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTextureSkinned* data, int startIndex, int elementCount)
    {
        // TQ-021: the XNA GetData<T>(T[], startIndex, elementCount) contract, as for the
        // built-in overloads above: startIndex indexes the destination array, the buffer is read
        // from its first vertex (CopyData IL_01eb-01f0), and a read past the buffer is an
        // InvalidOperationException (IL_00eb-0100).
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTextureSkinnedStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(0, packed.data(), 0, elementCount, sizeof(GpuVertex), 0);
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] = VertexPositionNormalTextureSkinned(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz), Vector2(v.u, v.v),
                Vector4(v.w0, v.w1, v.w2, v.w3),
                std::array<std::uint8_t, 4>{v.i0, v.i1, v.i2, v.i3});
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTexture* data, int count)
    {
        SetData(data, 0, count);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTexture* data, int startIndex, int elementCount)
    {
        // VertexPositionNormalTangentTexture has a virtual base (IVertexType). Pack into GPU
        // layout: float3 position + float3 normal + float4 tangent + float2 texcoord = 48 bytes.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTangentTextureStream;
        // TQ-021: the shared classic validation (disposed, null, bound, dataIndex, elementCount,
        // capacity) precedes any read of the caller's array or write of the shadow.
        if (!ValidateSetDataRange(data, startIndex, elementCount,
                                  sizeof(VertexPositionNormalTangentTexture), sizeof(GpuVertex),
                                  false, SetDataOptions::None, false))
        {
            return;
        }
        const VertexPositionNormalTangentTexture* const source = data + startIndex;
        const int count = elementCount;

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x  = source[i].Position.X;
            packed[i].y  = source[i].Position.Y;
            packed[i].z  = source[i].Position.Z;
            packed[i].nx = source[i].Normal.X;
            packed[i].ny = source[i].Normal.Y;
            packed[i].nz = source[i].Normal.Z;
            packed[i].tx = source[i].Tangent.X;
            packed[i].ty = source[i].Tangent.Y;
            packed[i].tz = source[i].Tangent.Z;
            packed[i].tw = source[i].Tangent.W;
            packed[i].u  = source[i].TextureCoordinate.X;
            packed[i].v  = source[i].TextureCoordinate.Y;
        }
        UploadValidatedData(packed.data(), count, sizeof(GpuVertex), SetDataOptions::None, false);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTexture* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTexture* data, int startIndex, int elementCount)
    {
        // TQ-021: the classic GetData<T> contract; see the skinned overload above.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTangentTextureStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(0, packed.data(), 0, elementCount, sizeof(GpuVertex), 0);
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] = VertexPositionNormalTangentTexture(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz),
                Vector4(v.tx, v.ty, v.tz, v.tw), Vector2(v.u, v.v));
        }
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTextureSkinned* data, int count)
    {
        SetData(data, 0, count);
    }

    void VertexBuffer::SetData(const VertexPositionNormalTangentTextureSkinned* data, int startIndex, int elementCount)
    {
        // VertexPositionNormalTangentTextureSkinned has a virtual base (IVertexType). Pack into
        // GPU layout: float3 position + float3 normal + float4 tangent + float2 texcoord +
        // float4 weights + byte4 indices = 68 bytes.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTangentTextureSkinnedStream;
        // TQ-021: validated exactly like the tangent overload above.
        if (!ValidateSetDataRange(data, startIndex, elementCount,
                                  sizeof(VertexPositionNormalTangentTextureSkinned),
                                  sizeof(GpuVertex), false, SetDataOptions::None, false))
        {
            return;
        }
        const VertexPositionNormalTangentTextureSkinned* const source = data + startIndex;
        const int count = elementCount;

        std::vector<GpuVertex> packed(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            packed[i].x  = source[i].Position.X;
            packed[i].y  = source[i].Position.Y;
            packed[i].z  = source[i].Position.Z;
            packed[i].nx = source[i].Normal.X;
            packed[i].ny = source[i].Normal.Y;
            packed[i].nz = source[i].Normal.Z;
            packed[i].tx = source[i].Tangent.X;
            packed[i].ty = source[i].Tangent.Y;
            packed[i].tz = source[i].Tangent.Z;
            packed[i].tw = source[i].Tangent.W;
            packed[i].u  = source[i].TextureCoordinate.X;
            packed[i].v  = source[i].TextureCoordinate.Y;
            packed[i].w0 = source[i].BlendWeight.X;
            packed[i].w1 = source[i].BlendWeight.Y;
            packed[i].w2 = source[i].BlendWeight.Z;
            packed[i].w3 = source[i].BlendWeight.W;
            packed[i].i0 = source[i].BlendIndices[0];
            packed[i].i1 = source[i].BlendIndices[1];
            packed[i].i2 = source[i].BlendIndices[2];
            packed[i].i3 = source[i].BlendIndices[3];
        }
        UploadValidatedData(packed.data(), count, sizeof(GpuVertex), SetDataOptions::None, false);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTextureSkinned* data, int count)
    {
        GetData(data, 0, count);
    }

    void VertexBuffer::GetData(VertexPositionNormalTangentTextureSkinned* data, int startIndex, int elementCount)
    {
        // TQ-021: the classic GetData<T> contract; see the skinned overload above.
        using GpuVertex = CNA::Internal::Graphics::PositionNormalTangentTextureSkinnedStream;
        ValidateClassicGetDataArguments(
            *this, bufferUsage_ == BufferUsage::WriteOnly,
            data, startIndex, elementCount, sizeof(GpuVertex));
        std::vector<GpuVertex> packed(static_cast<std::size_t>(elementCount));
        GetDataElementsAtInternal(0, packed.data(), 0, elementCount, sizeof(GpuVertex), 0);
        for (int i = 0; i < elementCount; ++i) {
            const GpuVertex& v = packed[static_cast<std::size_t>(i)];
            data[startIndex + i] = VertexPositionNormalTangentTextureSkinned(
                Vector3(v.x, v.y, v.z), Vector3(v.nx, v.ny, v.nz),
                Vector4(v.tx, v.ty, v.tz, v.tw), Vector2(v.u, v.v),
                Vector4(v.w0, v.w1, v.w2, v.w3),
                std::array<std::uint8_t, 4>{v.i0, v.i1, v.i2, v.i3});
        }
    }

    void VertexBuffer::SetDataRaw(const void* data, int count, int stride)
    {
        const std::size_t uploadStride =
            stride > 0 ? static_cast<std::size_t>(stride) : 0;
        if (!ValidateSetDataRange(
                data, 0, count, uploadStride, uploadStride, true,
                SetDataOptions::None, false))
        {
            return;
        }
        UploadValidatedData(
            data, count, uploadStride, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataRawWithOptions(const void* data, int startIndex, int elementCount,
                                            int stride, SetDataOptions options)
    {
        const std::size_t uploadStride =
            stride > 0 ? static_cast<std::size_t>(stride) : 0;
        if (!ValidateSetDataRange(
                data, startIndex, elementCount, uploadStride, uploadStride, false,
                options, true))
        {
            return;
        }
        const auto* source = static_cast<const std::uint8_t*>(data) +
                             static_cast<std::size_t>(startIndex) * uploadStride;
        UploadValidatedData(source, elementCount, uploadStride, options, true);
    }

    void VertexBuffer::SetDataRawAtWithOptions(const int offsetInBytes, const void* const data,
                                               const int startIndex, const int elementCount,
                                               const int stride, SetDataOptions options)
    {
        // Validated before any pointer arithmetic; SetDataRawAtEXT repeats the checks it owns.
        if (stride <= 0)
            throw System::ArgumentException("The vertex stride must be positive.", "stride");
        if (startIndex < 0)
            throw System::ArgumentOutOfRangeException(
                "startIndex", std::to_string(startIndex), "This parameter must not be negative.");
        (void) CheckedByteOffset(startIndex, static_cast<std::size_t>(stride));

        const auto* source =
            data == nullptr
                ? nullptr
                : static_cast<const std::uint8_t*>(data) +
                      static_cast<std::size_t>(startIndex) * static_cast<std::size_t>(stride);

        SetDataRawAtInternal(
            offsetInBytes, source, elementCount, stride, options, true);
    }

    void VertexBuffer::SetDataRawAtEXT(const int offsetInBytes, const void* const data,
                                       const int count, const int stride)
    {
        SetDataRawAtInternal(
            offsetInBytes, data, count, stride, SetDataOptions::None, false);
    }

    void VertexBuffer::SetDataRawAtInternal(const int offsetInBytes, const void* const data,
                                            const int count, const int stride,
                                            SetDataOptions options, bool useOptions)
    {
        const auto contextLease = LeaseRendererContext();
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException(DisposedObjectName(*this));
        if (stride <= 0)
            throw System::ArgumentException(
                "The vertex stride must be positive.", "stride");
        if (count < 0)
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count), "This parameter must not be negative.");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");
        const auto uploadStride = static_cast<std::size_t>(stride);
        if (static_cast<std::size_t>(offsetInBytes) % uploadStride != 0)
            throw System::ArgumentException(
                "The destination offset must fall on a vertex boundary.", "offsetInBytes");
        // The declaration, when there is one, is as binding here as it is for a whole-buffer
        // upload: a window written at a different stride would interleave with what is already
        // there rather than replace part of it.
        if (vertexDeclaration_.getVertexStrideProperty() > 0 &&
            static_cast<std::size_t>(vertexDeclaration_.getVertexStrideProperty()) != uploadStride)
        {
            throw System::ArgumentException(
                "The vertex stride does not match this VertexBuffer's VertexDeclaration.",
                "stride");
        }
        if (count == 0)
            return;
        if (data == nullptr)
            throw System::ArgumentNullException(
                "data", "This method does not accept null for this parameter.");
        ThrowIfSetDataResourceInUse(options, useOptions);

        const std::size_t capacity =
            CheckedByteCount(vertexCount_, uploadStride, "vertexCount");
        const std::size_t windowBytes = CheckedByteCount(count, uploadStride, "count");
        const std::size_t destinationOffset = static_cast<std::size_t>(offsetInBytes);
        if (destinationOffset > capacity || windowBytes > capacity - destinationOffset)
        {
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count),
                "The windowed upload exceeds the VertexBuffer's logical capacity.");
        }

        // The shadow is the only place a window can be composed, because the renderer contract
        // replaces whole-buffer contents. Growing it to the buffer's full capacity is what makes
        // never-written bytes read as zero rather than as whatever a shorter previous upload left.
        if (cpuShadow_.size() < capacity)
            cpuShadow_.resize(capacity, 0U);

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        std::copy(bytes, bytes + windowBytes,
                  cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes));

        renderer_->SetVertexDeclaration(vertexDeclaration_);
        renderer_->SetData(
            cpuShadow_.data(), static_cast<int>(capacity / uploadStride), uploadStride);
    }

    void VertexBuffer::SetDataElementsAtInternal(const int offsetInBytes,
                                                 const void* const data,
                                                 const int startIndex,
                                                 const int elementCount,
                                                 const std::size_t elementSize,
                                                 const int vertexStride,
                                                 SetDataOptions options,
                                                 const bool useOptions)
    {
        const auto contextLease = LeaseRendererContext();
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException(DisposedObjectName(*this));
        if (data == nullptr)
            throw System::ArgumentNullException(
                "data", "This method does not accept null for this parameter.");
        ThrowIfSetDataResourceInUse(options, useOptions);
        const std::size_t sourceByteOffset = CheckedByteOffset(startIndex, elementSize);
        ValidatePositiveElementCount(elementCount);
        (void) CheckedByteCount(elementCount, elementSize, "elementCount");

        const std::size_t resolvedStride =
            vertexStride == 0 ? elementSize : static_cast<std::size_t>(vertexStride);
        if (vertexStride < 0 || resolvedStride < elementSize)
            throw System::ArgumentOutOfRangeException(
                "vertexStride",
                "The vertex stride is too small for the type of data requested. This is not allowed.");

        const int declarationStride = vertexDeclaration_.getVertexStrideProperty();
        const std::size_t bufferStride =
            declarationStride > 0 ? static_cast<std::size_t>(declarationStride) : resolvedStride;
        const std::size_t capacity =
            CheckedByteCount(vertexCount_, bufferStride, "vertexCount");
        const std::size_t destinationOffset = static_cast<std::size_t>(offsetInBytes);
        if (destinationOffset > capacity)
            throw System::InvalidOperationException(
                "The array is not the correct size for the amount of data requested.");
        const std::size_t unsignedCount = static_cast<std::size_t>(elementCount);
        if (unsignedCount - 1 >
            (std::numeric_limits<std::size_t>::max() - elementSize) / resolvedStride)
        {
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "The strided upload range is too large.");
        }
        const std::size_t transferSpan =
            (unsignedCount - 1) * resolvedStride + elementSize;
        if (transferSpan > capacity - destinationOffset)
            throw System::InvalidOperationException(
                "The array is not the correct size for the amount of data requested.");

        if (cpuShadow_.size() < capacity)
            cpuShadow_.resize(capacity, 0U);
        const auto* source = static_cast<const std::uint8_t*>(data) + sourceByteOffset;
        for (std::size_t i = 0; i < unsignedCount; ++i)
        {
            std::copy(source + i * elementSize,
                      source + (i + 1) * elementSize,
                      cpuShadow_.begin() + static_cast<std::ptrdiff_t>(
                          destinationOffset + i * resolvedStride));
        }

        if (auto* const losable = dynamic_cast<CNA::Internal::Graphics::IContentLosable*>(this))
            losable->ClearContentLostEXT();
        renderer_->SetVertexDeclaration(vertexDeclaration_);
        renderer_->SetData(cpuShadow_.data(), vertexCount_, bufferStride);
    }

    void VertexBuffer::GetDataElementsAtInternal(const int offsetInBytes,
                                                 void* const data,
                                                 const int startIndex,
                                                 const int elementCount,
                                                 const std::size_t elementSize,
                                                 const int vertexStride) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException(DisposedObjectName(*this));
        if (data == nullptr)
            throw System::ArgumentNullException(
                "data", "This method does not accept null for this parameter.");
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");

        const std::size_t destinationByteOffset = CheckedByteOffset(startIndex, elementSize);
        ValidatePositiveElementCount(elementCount);
        (void) CheckedByteCount(elementCount, elementSize, "elementCount");
        const std::size_t resolvedStride =
            vertexStride == 0 ? elementSize : static_cast<std::size_t>(vertexStride);
        if (vertexStride < 0 || resolvedStride < elementSize)
            throw System::ArgumentOutOfRangeException(
                "vertexStride",
                "The vertex stride is too small for the type of data requested. This is not allowed.");

        const std::size_t sourceOffset = static_cast<std::size_t>(offsetInBytes);
        const std::size_t unsignedCount = static_cast<std::size_t>(elementCount);
        if (unsignedCount - 1 >
            (std::numeric_limits<std::size_t>::max() - elementSize) / resolvedStride)
        {
            throw System::ArgumentOutOfRangeException(
                "elementCount", std::to_string(elementCount),
                "The strided readback range is too large.");
        }
        const std::size_t transferSpan =
            (unsignedCount - 1) * resolvedStride + elementSize;
        if (sourceOffset > cpuShadow_.size() ||
            transferSpan > cpuShadow_.size() - sourceOffset)
        {
            throw System::InvalidOperationException(
                "The array is not the correct size for the amount of data requested.");
        }

        auto* destination = static_cast<std::uint8_t*>(data) + destinationByteOffset;
        for (std::size_t i = 0; i < unsignedCount; ++i)
        {
            std::copy(cpuShadow_.begin() + static_cast<std::ptrdiff_t>(
                          sourceOffset + i * resolvedStride),
                      cpuShadow_.begin() + static_cast<std::ptrdiff_t>(
                          sourceOffset + i * resolvedStride + elementSize),
                      destination + i * elementSize);
        }
    }

    void VertexBuffer::GetDataRawEXT(const int offsetInBytes, void* const destination,
                                     const int count, const int stride) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException(DisposedObjectName(*this));
        if (bufferUsage_ == BufferUsage::WriteOnly)
            throw System::NotSupportedException(
                "Calling GetData on a resource that was created with BufferUsage.WriteOnly is not supported.");
        if (stride <= 0)
            throw System::ArgumentException(
                "The vertex stride must be positive.", "stride");
        if (count < 0)
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count), "This parameter must not be negative.");
        if (offsetInBytes < 0)
            throw System::ArgumentOutOfRangeException(
                "offsetInBytes", std::to_string(offsetInBytes),
                "This parameter must not be negative.");
        if (count == 0)
            return;
        if (destination == nullptr)
            throw System::ArgumentNullException("destination");

        const std::size_t windowBytes =
            CheckedByteCount(count, static_cast<std::size_t>(stride), "count");
        if (static_cast<std::size_t>(offsetInBytes) > cpuShadow_.size() ||
            windowBytes > cpuShadow_.size() - static_cast<std::size_t>(offsetInBytes))
        {
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count),
                "The requested window is outside the data this VertexBuffer holds.");
        }
        std::copy(cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes),
                  cpuShadow_.begin() + static_cast<std::ptrdiff_t>(offsetInBytes) +
                      static_cast<std::ptrdiff_t>(windowBytes),
                  static_cast<std::uint8_t*>(destination));
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionColor* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionColorTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionNormalTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }

    void VertexBuffer::SetDataWithOptions(const VertexPositionTexture* data, int startIndex,
                                          int elementCount, SetDataOptions options)
    {
        SetDataInternal(data, startIndex, elementCount, options, true);
    }
}
