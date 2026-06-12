// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"

#include <stdexcept>
#include <utility>

namespace Microsoft::Xna::Framework::Graphics
{
    DisplayModeCollection::DisplayModeCollection()
        : modes_()
    {
    }

    DisplayModeCollection::DisplayModeCollection(std::vector<DisplayMode> modes)
        : modes_(std::move(modes))
    {
    }

    SharpRuntime::intcs DisplayModeCollection::getCountProperty() const
    {
        return static_cast<SharpRuntime::intcs>(modes_.size());
    }

    const DisplayMode& DisplayModeCollection::operator[](SharpRuntime::intcs index) const
    {
        if (index < 0 || index >= getCountProperty())
        {
            throw std::out_of_range("index");
        }

        return modes_[static_cast<std::size_t>(index)];
    }

    DisplayModeCollection::const_iterator DisplayModeCollection::begin() const
    {
        return modes_.begin();
    }

    DisplayModeCollection::const_iterator DisplayModeCollection::end() const
    {
        return modes_.end();
    }

    const std::string& DisplayModeCollection::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Graphics.DisplayModeCollection";
        return typeName;
    }
}
