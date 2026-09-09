// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/NotSupportedException.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief A typed, CPU-writable constant-buffer view over the shared buffer resource.
     *
     * The value's object representation is copied unchanged. Trivial copying and standard
     * layout make that operation defined, but the caller remains responsible for declaring
     * fields and explicit padding that match the shader language's block-layout rules.
     * Allocation is rounded up to the renderer's published uniform-buffer offset alignment;
     * padding bytes are zeroed on every upload.
     *
     * @tparam T Trivially-copyable standard-layout shader block value.
     */
    template<typename T>
        requires (std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>)
    class ConstantBufferT
    {
    public:
        /**
         * @brief Allocates one aligned constant-buffer value.
         * @param device Device that owns the shared buffer resource.
         * @throws System::NotSupportedException If the renderer exposes no usable uniform-buffer
         *         size/alignment contract or the aligned value exceeds its maximum range.
         */
        explicit ConstantBufferT(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
            : buffer_(device, makeDescriptor(device))
        {
        }

        /**
         * @brief Replaces the complete typed value and zeroes allocation padding.
         * @param value Value whose object representation is uploaded.
         */
        void setData(const T& value)
        {
            std::vector<std::uint8_t> bytes(buffer_.getByteSize(), UINT8_C(0));
            std::memcpy(bytes.data(), &value, sizeof(T));
            buffer_.setBytes(bytes.data(), bytes.size());
        }

        /**
         * @brief Returns the mutable shared buffer used by binding and transfer APIs.
         * @return Underlying tracked buffer with `Constant` usage.
         */
        [[nodiscard]] StorageBuffer& getBuffer() { return buffer_; }

        /**
         * @brief Returns the shared buffer used by binding and transfer APIs.
         * @return Underlying tracked buffer with `Constant` usage.
         */
        [[nodiscard]] const StorageBuffer& getBuffer() const { return buffer_; }

    private:
        [[nodiscard]] static StorageBufferDescriptor makeDescriptor(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
        {
            const CNA::RendererLimitValue maximum =
                device.GetRendererLimitEXT(CNA::RendererLimit::MaxUniformBufferBytes);
            const CNA::RendererLimitValue alignment =
                device.GetRendererLimitEXT(
                    CNA::RendererLimit::MinUniformBufferOffsetAlignment);
            const std::string renderer(device.GetGraphicsRendererName());
            if (!maximum.known || maximum.value == 0)
                throw System::NotSupportedException(
                    "CNA::Graphics::ConstantBufferT: the '" + renderer +
                    "' renderer exposes no implemented maximum constant-buffer size");
            if (!alignment.known || alignment.value == 0 ||
                alignment.value > std::numeric_limits<std::size_t>::max())
                throw System::NotSupportedException(
                    "CNA::Graphics::ConstantBufferT: the '" + renderer +
                    "' renderer exposes no usable constant-buffer alignment");

            const auto nativeAlignment = static_cast<std::size_t>(alignment.value);
            const std::size_t remainder = sizeof(T) % nativeAlignment;
            const std::size_t padding = remainder == 0 ? 0 : nativeAlignment - remainder;
            if (sizeof(T) > std::numeric_limits<std::size_t>::max() - padding)
                throw System::NotSupportedException(
                    "CNA::Graphics::ConstantBufferT: aligned allocation size overflows size_t");
            const std::size_t allocatedBytes = sizeof(T) + padding;
            if (static_cast<std::uint64_t>(allocatedBytes) > maximum.value)
                throw System::NotSupportedException(
                    "CNA::Graphics::ConstantBufferT: aligned byte size " +
                    std::to_string(allocatedBytes) + " exceeds the device maximum of " +
                    std::to_string(maximum.value));
            return StorageBufferDescriptor(
                allocatedBytes, StorageBufferUsage::Constant,
                StorageBufferCpuAccess::Write);
        }

        StorageBuffer buffer_;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
