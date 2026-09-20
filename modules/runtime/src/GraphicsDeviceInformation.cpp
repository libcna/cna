// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"

#include <cstdint>
#include <type_traits>

namespace Microsoft::Xna::Framework
{
    GraphicsDeviceInformation::GraphicsDeviceInformation()
        : adapter_(nullptr),
          graphicsProfile_(Graphics::GraphicsProfile::Reach),
          presentationParameters_()
    {
    }

    Graphics::GraphicsAdapter* GraphicsDeviceInformation::getAdapterProperty() const
    {
        return adapter_;
    }

    void GraphicsDeviceInformation::setAdapterProperty(Graphics::GraphicsAdapter* value)
    {
        adapter_ = value;
    }

    Graphics::GraphicsProfile GraphicsDeviceInformation::getGraphicsProfileProperty() const
    {
        return graphicsProfile_;
    }

    void GraphicsDeviceInformation::setGraphicsProfileProperty(Graphics::GraphicsProfile value)
    {
        graphicsProfile_ = value;
    }

    Graphics::PresentationParameters& GraphicsDeviceInformation::getPresentationParametersProperty()
    {
        return presentationParameters_;
    }

    const Graphics::PresentationParameters& GraphicsDeviceInformation::getPresentationParametersProperty() const
    {
        return presentationParameters_;
    }

    void GraphicsDeviceInformation::setPresentationParametersProperty(const Graphics::PresentationParameters& value)
    {
        presentationParameters_ = value;
    }

    GraphicsDeviceInformation GraphicsDeviceInformation::Clone() const
    {
        GraphicsDeviceInformation clone;
        clone.adapter_ = adapter_;
        clone.graphicsProfile_ = graphicsProfile_;
        clone.presentationParameters_ = presentationParameters_.Clone();
        return clone;
    }

    namespace
    {
        /// Every field XNA XORs into this hash is an enum, an Int32, a Boolean or an IntPtr, and
        /// each of those hashes to its own numeric value truncated to Int32 -- Enum.GetHashCode()
        /// over an Int32-backed enum, Int32.GetHashCode(), Boolean.GetHashCode() (1 or 0) and
        /// IntPtr.GetHashCode() all do exactly that.
        template <typename T>
        int FieldHash(const T& value)
        {
            if constexpr (std::is_enum_v<T>)
            {
                return static_cast<int>(static_cast<std::underlying_type_t<T>>(value));
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                return value ? 1 : 0;
            }
            else
            {
                return static_cast<int>(static_cast<std::uint32_t>(value));
            }
        }
    }

    bool GraphicsDeviceInformation::Equals(const System::Object* obj) const
    {
        const auto* other = dynamic_cast<const GraphicsDeviceInformation*>(obj);
        if (other == nullptr)
        {
            return false;
        }
        if (other->adapter_ != adapter_ || other->graphicsProfile_ != graphicsProfile_)
        {
            return false;
        }

        const Graphics::PresentationParameters& mine = presentationParameters_;
        const Graphics::PresentationParameters& theirs = other->presentationParameters_;
        return mine.getBackBufferWidthProperty() == theirs.getBackBufferWidthProperty() &&
               mine.getBackBufferHeightProperty() == theirs.getBackBufferHeightProperty() &&
               mine.getBackBufferFormatProperty() == theirs.getBackBufferFormatProperty() &&
               mine.getDepthStencilFormatProperty() == theirs.getDepthStencilFormatProperty() &&
               mine.getMultiSampleCountProperty() == theirs.getMultiSampleCountProperty() &&
               mine.getDisplayOrientationProperty() == theirs.getDisplayOrientationProperty() &&
               mine.getPresentationIntervalProperty() == theirs.getPresentationIntervalProperty() &&
               mine.getRenderTargetUsageProperty() == theirs.getRenderTargetUsageProperty() &&
               mine.getDeviceWindowHandleProperty() == theirs.getDeviceWindowHandleProperty() &&
               mine.getIsFullScreenProperty() == theirs.getIsFullScreenProperty();
    }

    int GraphicsDeviceInformation::GetHashCode() const
    {
        // XNA XORs the profile, the adapter's identity hash, and the same ten presentation
        // fields Equals() compares, in that order. The adapter is a reference type in XNA and a
        // pointer here, so its identity hash is the pointer value.
        const auto adapterIdentity = reinterpret_cast<std::uintptr_t>(adapter_);
        return FieldHash(graphicsProfile_) ^
               FieldHash(adapterIdentity) ^
               FieldHash(presentationParameters_.getBackBufferWidthProperty()) ^
               FieldHash(presentationParameters_.getBackBufferHeightProperty()) ^
               FieldHash(presentationParameters_.getBackBufferFormatProperty()) ^
               FieldHash(presentationParameters_.getDepthStencilFormatProperty()) ^
               FieldHash(presentationParameters_.getMultiSampleCountProperty()) ^
               FieldHash(presentationParameters_.getDisplayOrientationProperty()) ^
               FieldHash(presentationParameters_.getPresentationIntervalProperty()) ^
               FieldHash(presentationParameters_.getRenderTargetUsageProperty()) ^
               FieldHash(presentationParameters_.getDeviceWindowHandleProperty()) ^
               FieldHash(presentationParameters_.getIsFullScreenProperty());
    }

    const std::string& GraphicsDeviceInformation::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GraphicsDeviceInformation";
        return typeName;
    }
}
