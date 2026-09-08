// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "SoftwareFramebufferErrors.hpp"

#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        using Microsoft::Xna::Framework::Graphics::PackedVector::HalfTypeHelper;

        [[nodiscard]] std::uint16_t ReadUInt16(const void* data)
        {
            std::uint16_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }

        void WriteUInt16(void* data, std::uint16_t value)
        {
            std::memcpy(data, &value, sizeof(value));
        }

        [[nodiscard]] std::uint32_t ReadUInt32(const void* data)
        {
            std::uint32_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }

        void WriteUInt32(void* data, std::uint32_t value)
        {
            std::memcpy(data, &value, sizeof(value));
        }

        [[nodiscard]] float ReadFloat(const void* data)
        {
            float value = 0.0f;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }

        void WriteFloat(void* data, float value)
        {
            std::memcpy(data, &value, sizeof(value));
        }

        [[nodiscard]] std::uint8_t ToUnorm8(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        }

        [[nodiscard]] std::uint32_t ToUnorm(float value, std::uint32_t maximum)
        {
            return static_cast<std::uint32_t>(
                std::clamp(value, 0.0f, 1.0f) * static_cast<float>(maximum) + 0.5f);
        }

        [[nodiscard]] float QuantizeHalf(float value)
        {
            return HalfTypeHelper::Convert(HalfTypeHelper::Convert(value));
        }
    }

    void SoftwareFramebuffer::Resize(int w, int h)
    {
        const SoftwareFramebufferAllocationRequest request{
            w, h, allocateDepthStorage, allocateStencilStorage, multiSampleCount, false,
            HasWideColor()};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        std::vector<std::uint8_t> newColor;
        std::vector<float> newWideColor;
        std::vector<float> newDepth;
        std::vector<std::uint8_t> newStencil;
        std::vector<std::uint8_t> newMultiSampleColor;
        std::vector<float> newMultiSampleWideColor;
        std::vector<float> newMultiSampleDepth;
        std::vector<std::uint8_t> newMultiSampleStencil;
        try
        {
            newColor.assign(layout.colorBytes, 0u);
            newWideColor.assign(layout.wideColorElementCount, 0.0f);
            newDepth.assign(layout.depthElementCount, 1.0f);
            newStencil.assign(layout.stencilBytes, 0u);
            newMultiSampleColor.assign(layout.multiSampleBytes, 0u);
            newMultiSampleWideColor.assign(layout.multiSampleWideColorElementCount, 0.0f);
            newMultiSampleDepth.assign(layout.multiSampleDepthElementCount, 1.0f);
            newMultiSampleStencil.assign(layout.multiSampleStencilBytes, 0u);
        }
        catch (const std::bad_alloc&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }

        width = w;
        height = h;
        color = std::move(newColor);
        wideColor = std::move(newWideColor);
        depthBuffer = std::move(newDepth);
        stencilBuffer = std::move(newStencil);
        multiSampleColor = std::move(newMultiSampleColor);
        multiSampleWideColor = std::move(newMultiSampleWideColor);
        multiSampleDepthBuffer = std::move(newMultiSampleDepth);
        multiSampleStencilBuffer = std::move(newMultiSampleStencil);
    }

    void SoftwareFramebuffer::SetMultiSampleCount(int sampleCount)
    {
        // CPU MSAA deliberately has one high-quality, predictable option: a rotated-independent
        // 2x2 grid. Treat every other request as unsupported rather than silently claiming an
        // arbitrary count with a different number of actual samples.
        const int appliedCount = sampleCount == 4 ? 4 : 0;
        if (multiSampleCount == appliedCount)
            return;

        if (appliedCount == 0)
        {
            // Preserve the last rendered image when an application turns the optional feature
            // back off at reset time; otherwise the unresolved colour plane would be discarded.
            ResolveColor();
            const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                           static_cast<std::size_t>(height);
            if (!multiSampleDepthBuffer.empty())
            {
                for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
                    depthBuffer[pixel] = multiSampleDepthBuffer[pixel * 4u];
            }
            if (!multiSampleStencilBuffer.empty())
            {
                for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
                    stencilBuffer[pixel] = multiSampleStencilBuffer[pixel * 4u];
            }
            multiSampleCount = 0;
            std::vector<std::uint8_t>().swap(multiSampleColor);
            std::vector<float>().swap(multiSampleWideColor);
            std::vector<float>().swap(multiSampleDepthBuffer);
            std::vector<std::uint8_t>().swap(multiSampleStencilBuffer);
            return;
        }

        const SoftwareFramebufferAllocationRequest request{
            width, height, allocateDepthStorage, allocateStencilStorage, appliedCount, false,
            HasWideColor()};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        std::vector<std::uint8_t> newMultiSampleColor;
        std::vector<float> newMultiSampleWideColor;
        std::vector<float> newMultiSampleDepth;
        std::vector<std::uint8_t> newMultiSampleStencil;
        try
        {
            newMultiSampleColor.resize(layout.multiSampleBytes);
            newMultiSampleWideColor.resize(layout.multiSampleWideColorElementCount);
            newMultiSampleDepth.resize(layout.multiSampleDepthElementCount);
            newMultiSampleStencil.resize(layout.multiSampleStencilBytes);
        }
        catch (const std::bad_alloc&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }

        multiSampleColor = std::move(newMultiSampleColor);
        multiSampleWideColor = std::move(newMultiSampleWideColor);
        multiSampleDepthBuffer = std::move(newMultiSampleDepth);
        multiSampleStencilBuffer = std::move(newMultiSampleStencil);
        multiSampleCount = appliedCount;
        CopyResolvedColorToMultiSample();
        const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            for (int sample = 0; sample < 4; ++sample)
            {
                const std::size_t sampleIndex = pixel * 4u +
                                                static_cast<std::size_t>(sample);
                if (!multiSampleDepthBuffer.empty())
                    multiSampleDepthBuffer[sampleIndex] = depthBuffer[pixel];
                if (!multiSampleStencilBuffer.empty())
                    multiSampleStencilBuffer[sampleIndex] = stencilBuffer[pixel];
            }
        }
    }

    int SoftwareFramebuffer::DeclaredColorTexelSize() const
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Single:
            case SurfaceFormat::HalfVector2:
                return 4;
            case SurfaceFormat::HalfSingle:
                return 2;
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return 8;
            case SurfaceFormat::Vector4:
                return 16;
            default:
                throw std::runtime_error(
                    "SoftwareFramebuffer: unsupported render-target SurfaceFormat ordinal " +
                    std::to_string(surfaceFormat));
        }
    }

    std::array<float, 4> SoftwareFramebuffer::QuantizeColor(
        const std::array<float, 4>& value) const
    {
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
                return {ToUnorm8(value[0]) / 255.0f,
                        ToUnorm8(value[1]) / 255.0f,
                        ToUnorm8(value[2]) / 255.0f,
                        ToUnorm8(value[3]) / 255.0f};
            case SurfaceFormat::Rgba1010102:
                return {ToUnorm(value[0], 1023u) / 1023.0f,
                        ToUnorm(value[1], 1023u) / 1023.0f,
                        ToUnorm(value[2], 1023u) / 1023.0f,
                        ToUnorm(value[3], 3u) / 3.0f};
            case SurfaceFormat::Rg32:
                return {ToUnorm(value[0], 65535u) / 65535.0f,
                        ToUnorm(value[1], 65535u) / 65535.0f, 1.0f, 1.0f};
            case SurfaceFormat::Rgba64:
                return {ToUnorm(value[0], 65535u) / 65535.0f,
                        ToUnorm(value[1], 65535u) / 65535.0f,
                        ToUnorm(value[2], 65535u) / 65535.0f,
                        ToUnorm(value[3], 65535u) / 65535.0f};
            case SurfaceFormat::Single:
                return {value[0], 1.0f, 1.0f, 1.0f};
            case SurfaceFormat::Vector2:
                return {value[0], value[1], 1.0f, 1.0f};
            case SurfaceFormat::Vector4:
                return value;
            case SurfaceFormat::HalfSingle:
                return {QuantizeHalf(value[0]), 1.0f, 1.0f, 1.0f};
            case SurfaceFormat::HalfVector2:
                return {QuantizeHalf(value[0]), QuantizeHalf(value[1]), 1.0f, 1.0f};
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return {QuantizeHalf(value[0]), QuantizeHalf(value[1]),
                        QuantizeHalf(value[2]), QuantizeHalf(value[3])};
            default:
                (void)DeclaredColorTexelSize();
                return value;
        }
    }

    std::array<float, 4> SoftwareFramebuffer::DecodeDeclaredColor(const void* data) const
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
                return {bytes[0] / 255.0f, bytes[1] / 255.0f,
                        bytes[2] / 255.0f, bytes[3] / 255.0f};
            case SurfaceFormat::Rgba1010102:
            {
                const std::uint32_t packed = ReadUInt32(bytes);
                return {static_cast<float>(packed & 0x3FFu) / 1023.0f,
                        static_cast<float>((packed >> 10u) & 0x3FFu) / 1023.0f,
                        static_cast<float>((packed >> 20u) & 0x3FFu) / 1023.0f,
                        static_cast<float>((packed >> 30u) & 0x3u) / 3.0f};
            }
            case SurfaceFormat::Rg32:
                return {ReadUInt16(bytes) / 65535.0f,
                        ReadUInt16(bytes + 2) / 65535.0f, 1.0f, 1.0f};
            case SurfaceFormat::Rgba64:
                return {ReadUInt16(bytes) / 65535.0f,
                        ReadUInt16(bytes + 2) / 65535.0f,
                        ReadUInt16(bytes + 4) / 65535.0f,
                        ReadUInt16(bytes + 6) / 65535.0f};
            case SurfaceFormat::Single:
                return {ReadFloat(bytes), 1.0f, 1.0f, 1.0f};
            case SurfaceFormat::Vector2:
                return {ReadFloat(bytes), ReadFloat(bytes + 4), 1.0f, 1.0f};
            case SurfaceFormat::Vector4:
                return {ReadFloat(bytes), ReadFloat(bytes + 4),
                        ReadFloat(bytes + 8), ReadFloat(bytes + 12)};
            case SurfaceFormat::HalfSingle:
                return {HalfTypeHelper::Convert(ReadUInt16(bytes)), 1.0f, 1.0f, 1.0f};
            case SurfaceFormat::HalfVector2:
                return {HalfTypeHelper::Convert(ReadUInt16(bytes)),
                        HalfTypeHelper::Convert(ReadUInt16(bytes + 2)), 1.0f, 1.0f};
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return {HalfTypeHelper::Convert(ReadUInt16(bytes)),
                        HalfTypeHelper::Convert(ReadUInt16(bytes + 2)),
                        HalfTypeHelper::Convert(ReadUInt16(bytes + 4)),
                        HalfTypeHelper::Convert(ReadUInt16(bytes + 6))};
            default:
                (void)DeclaredColorTexelSize();
                return {};
        }
    }

    void SoftwareFramebuffer::EncodeDeclaredColor(
        const std::array<float, 4>& value, void* data) const
    {
        auto* bytes = static_cast<std::uint8_t*>(data);
        const std::array<float, 4> stored = QuantizeColor(value);
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
                bytes[0] = ToUnorm8(stored[0]);
                bytes[1] = ToUnorm8(stored[1]);
                bytes[2] = ToUnorm8(stored[2]);
                bytes[3] = ToUnorm8(stored[3]);
                return;
            case SurfaceFormat::Rgba1010102:
                WriteUInt32(bytes,
                            ToUnorm(stored[0], 1023u) |
                            (ToUnorm(stored[1], 1023u) << 10u) |
                            (ToUnorm(stored[2], 1023u) << 20u) |
                            (ToUnorm(stored[3], 3u) << 30u));
                return;
            case SurfaceFormat::Rg32:
                WriteUInt16(bytes, static_cast<std::uint16_t>(ToUnorm(stored[0], 65535u)));
                WriteUInt16(bytes + 2,
                            static_cast<std::uint16_t>(ToUnorm(stored[1], 65535u)));
                return;
            case SurfaceFormat::Rgba64:
                WriteUInt16(bytes, static_cast<std::uint16_t>(ToUnorm(stored[0], 65535u)));
                WriteUInt16(bytes + 2,
                            static_cast<std::uint16_t>(ToUnorm(stored[1], 65535u)));
                WriteUInt16(bytes + 4,
                            static_cast<std::uint16_t>(ToUnorm(stored[2], 65535u)));
                WriteUInt16(bytes + 6,
                            static_cast<std::uint16_t>(ToUnorm(stored[3], 65535u)));
                return;
            case SurfaceFormat::Single:
                WriteFloat(bytes, stored[0]);
                return;
            case SurfaceFormat::Vector2:
                WriteFloat(bytes, stored[0]);
                WriteFloat(bytes + 4, stored[1]);
                return;
            case SurfaceFormat::Vector4:
                WriteFloat(bytes, stored[0]);
                WriteFloat(bytes + 4, stored[1]);
                WriteFloat(bytes + 8, stored[2]);
                WriteFloat(bytes + 12, stored[3]);
                return;
            case SurfaceFormat::HalfSingle:
                WriteUInt16(bytes, HalfTypeHelper::Convert(stored[0]));
                return;
            case SurfaceFormat::HalfVector2:
                WriteUInt16(bytes, HalfTypeHelper::Convert(stored[0]));
                WriteUInt16(bytes + 2, HalfTypeHelper::Convert(stored[1]));
                return;
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                WriteUInt16(bytes, HalfTypeHelper::Convert(stored[0]));
                WriteUInt16(bytes + 2, HalfTypeHelper::Convert(stored[1]));
                WriteUInt16(bytes + 4, HalfTypeHelper::Convert(stored[2]));
                WriteUInt16(bytes + 6, HalfTypeHelper::Convert(stored[3]));
                return;
            default:
                (void)DeclaredColorTexelSize();
                return;
        }
    }

    std::array<float, 4> SoftwareFramebuffer::ReadColor(
        std::size_t pixel, int sample) const
    {
        if (HasWideColor())
        {
            const std::vector<float>& storage = sample >= 0
                ? multiSampleWideColor : wideColor;
            const std::size_t base = sample >= 0
                ? (pixel * 4u + static_cast<std::size_t>(sample)) * 4u
                : pixel * 4u;
            return {storage[base], storage[base + 1], storage[base + 2], storage[base + 3]};
        }

        const std::vector<std::uint8_t>& storage = sample >= 0
            ? multiSampleColor : color;
        const std::size_t base = sample >= 0
            ? (pixel * 4u + static_cast<std::size_t>(sample)) * 4u
            : pixel * 4u;
        return {storage[base] / 255.0f, storage[base + 1] / 255.0f,
                storage[base + 2] / 255.0f, storage[base + 3] / 255.0f};
    }

    void SoftwareFramebuffer::WriteColor(
        std::size_t pixel, int sample, const std::array<float, 4>& value,
        int colorWriteMask) const
    {
        std::array<float, 4> merged = ReadColor(pixel, sample);
        for (int channel = 0; channel < 4; ++channel)
        {
            if ((colorWriteMask & (1 << channel)) != 0)
                merged[static_cast<std::size_t>(channel)] = value[static_cast<std::size_t>(channel)];
        }
        const std::array<float, 4> stored = QuantizeColor(merged);
        const std::size_t base = sample >= 0
            ? (pixel * 4u + static_cast<std::size_t>(sample)) * 4u
            : pixel * 4u;

        if (HasWideColor())
        {
            std::vector<float>& storage = sample >= 0 ? multiSampleWideColor : wideColor;
            std::copy(stored.begin(), stored.end(), storage.begin() +
                      static_cast<std::ptrdiff_t>(base));
        }

        std::vector<std::uint8_t>& mirror = sample >= 0 ? multiSampleColor : color;
        for (int channel = 0; channel < 4; ++channel)
            mirror[base + static_cast<std::size_t>(channel)] =
                ToUnorm8(stored[static_cast<std::size_t>(channel)]);
    }

    void SoftwareFramebuffer::LoadDeclaredColor(const std::uint8_t* data, int stride)
    {
        const int texelSize = DeclaredColorTexelSize();
        const int rowBytes = width * texelSize;
        const int sourceStride = stride > 0 ? stride : rowBytes;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::uint8_t* texel = data + static_cast<std::size_t>(y) * sourceStride +
                                            static_cast<std::size_t>(x) * texelSize;
                WriteColor(static_cast<std::size_t>(y) * width + x, -1,
                           DecodeDeclaredColor(texel), 0x0F);
            }
        }
        CopyResolvedColorToMultiSample();
    }

    void SoftwareFramebuffer::StoreDeclaredColor(
        int x, int y, int w, int h, void* data) const
    {
        const int texelSize = DeclaredColorTexelSize();
        auto* destination = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            for (int column = 0; column < w; ++column)
            {
                const std::size_t pixel =
                    static_cast<std::size_t>(y + row) * width + (x + column);
                EncodeDeclaredColor(ReadColor(pixel),
                                    destination +
                                        (static_cast<std::size_t>(row) * w + column) * texelSize);
            }
        }
    }

    void SoftwareFramebuffer::CopyResolvedColorToMultiSample()
    {
        if (!HasMultiSampleColor())
            return;

        const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::size_t resolvedIndex = pixel * 4u;
            for (int sample = 0; sample < 4; ++sample)
            {
                const std::size_t sampleIndex = (pixel * 4u + static_cast<std::size_t>(sample)) * 4u;
                multiSampleColor[sampleIndex + 0] = color[resolvedIndex + 0];
                multiSampleColor[sampleIndex + 1] = color[resolvedIndex + 1];
                multiSampleColor[sampleIndex + 2] = color[resolvedIndex + 2];
                multiSampleColor[sampleIndex + 3] = color[resolvedIndex + 3];
                if (HasWideColor())
                {
                    multiSampleWideColor[sampleIndex + 0] = wideColor[resolvedIndex + 0];
                    multiSampleWideColor[sampleIndex + 1] = wideColor[resolvedIndex + 1];
                    multiSampleWideColor[sampleIndex + 2] = wideColor[resolvedIndex + 2];
                    multiSampleWideColor[sampleIndex + 3] = wideColor[resolvedIndex + 3];
                }
            }
        }
    }

    void SoftwareFramebuffer::ResolveColor() const
    {
        if (!HasMultiSampleColor())
            return;

        const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            if (HasWideColor())
            {
                std::array<float, 4> resolved{};
                for (int sample = 0; sample < 4; ++sample)
                {
                    const std::array<float, 4> value = ReadColor(pixel, sample);
                    for (int channel = 0; channel < 4; ++channel)
                        resolved[static_cast<std::size_t>(channel)] +=
                            value[static_cast<std::size_t>(channel)] * 0.25f;
                }
                WriteColor(pixel, -1, resolved, 0x0F);
                continue;
            }
            const std::size_t resolvedIndex = pixel * 4u;
            for (int channel = 0; channel < 4; ++channel)
            {
                unsigned int sum = 0;
                for (int sample = 0; sample < 4; ++sample)
                    sum += multiSampleColor[(pixel * 4u + static_cast<std::size_t>(sample)) * 4u +
                                            static_cast<std::size_t>(channel)];
                color[resolvedIndex + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(sum / 4u);
            }
        }
    }

    void SoftwareFramebuffer::ClearColor(float r, float g, float b, float a)
    {
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            WriteColor(i, -1, {r, g, b, a}, 0x0F);
            if (HasMultiSampleColor())
            {
                for (int sample = 0; sample < 4; ++sample)
                    WriteColor(i, sample, {r, g, b, a}, 0x0F);
            }
        }
    }

    void SoftwareFramebuffer::ClearDepthValue(float depthValue)
    {
        std::fill(depthBuffer.begin(), depthBuffer.end(), depthValue);
        std::fill(multiSampleDepthBuffer.begin(), multiSampleDepthBuffer.end(), depthValue);
    }

    void SoftwareFramebuffer::ClearStencilValue(int stencilValue)
    {
        std::fill(stencilBuffer.begin(), stencilBuffer.end(),
                  static_cast<std::uint8_t>(std::clamp(stencilValue, 0, 255)));
        std::fill(multiSampleStencilBuffer.begin(), multiSampleStencilBuffer.end(),
                  static_cast<std::uint8_t>(std::clamp(stencilValue, 0, 255)));
    }
}
