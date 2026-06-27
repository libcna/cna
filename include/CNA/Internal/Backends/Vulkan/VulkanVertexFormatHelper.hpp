// SPDX-License-Identifier: MS-PL
#pragma once

#include <vulkan/vulkan.h>
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"

namespace CNA::Internal::Backends::Vulkan
{
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;

    /**
     * Maps an XNA VertexElementFormat to the corresponding VkFormat used as a
     * Vulkan vertex input attribute description.
     *
     * These mappings must match the hardcoded VkVertexInputAttributeDescription
     * arrays in VulkanGraphicsBackend pipeline creation functions.  The explicit
     * mapping here makes the contract testable and prevents silent mismatches
     * between the C++ vertex packing and the SPIR-V shader attribute locations.
     */
    inline VkFormat VertexElementFormatToVk(VertexElementFormat fmt)
    {
        switch (fmt) {
            case VertexElementFormat::Single:           return VK_FORMAT_R32_SFLOAT;
            case VertexElementFormat::Vector2:          return VK_FORMAT_R32G32_SFLOAT;
            case VertexElementFormat::Vector3:          return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexElementFormat::Vector4:          return VK_FORMAT_R32G32B32A32_SFLOAT;
            case VertexElementFormat::Color:            return VK_FORMAT_R8G8B8A8_UNORM;
            case VertexElementFormat::Byte4:            return VK_FORMAT_R8G8B8A8_UINT;
            case VertexElementFormat::Short2:           return VK_FORMAT_R16G16_SINT;
            case VertexElementFormat::Short4:           return VK_FORMAT_R16G16B16A16_SINT;
            case VertexElementFormat::NormalizedShort2: return VK_FORMAT_R16G16_SNORM;
            case VertexElementFormat::NormalizedShort4: return VK_FORMAT_R16G16B16A16_SNORM;
            case VertexElementFormat::HalfVector2:      return VK_FORMAT_R16G16_SFLOAT;
            case VertexElementFormat::HalfVector4:      return VK_FORMAT_R16G16B16A16_SFLOAT;
            default:                                    return VK_FORMAT_UNDEFINED;
        }
    }

    /**
     * Returns the size in bytes of the data type described by a VertexElementFormat.
     * Matches the FNA GetTypeSize() logic used in VertexDeclaration stride auto-computation.
     */
    inline int VertexElementFormatSize(VertexElementFormat fmt)
    {
        switch (fmt) {
            case VertexElementFormat::Single:           return 4;
            case VertexElementFormat::Vector2:          return 8;
            case VertexElementFormat::Vector3:          return 12;
            case VertexElementFormat::Vector4:          return 16;
            case VertexElementFormat::Color:            return 4;
            case VertexElementFormat::Byte4:            return 4;
            case VertexElementFormat::Short2:           return 4;
            case VertexElementFormat::Short4:           return 8;
            case VertexElementFormat::NormalizedShort2: return 4;
            case VertexElementFormat::NormalizedShort4: return 8;
            case VertexElementFormat::HalfVector2:      return 4;
            case VertexElementFormat::HalfVector4:      return 8;
            default:                                    return 0;
        }
    }

} // namespace CNA::Internal::Backends::Vulkan
