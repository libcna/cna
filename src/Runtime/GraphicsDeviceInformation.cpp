// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"

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

    const std::string& GraphicsDeviceInformation::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.GraphicsDeviceInformation";
        return typeName;
    }
}
