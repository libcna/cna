// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/TextDropEventArgsEXT.hpp"

#include <utility>

namespace Microsoft::Xna::Framework
{
    TextDropEventArgsEXT::TextDropEventArgsEXT(SharpRuntime::String text) : text_(std::move(text))
    {
    }

    const SharpRuntime::String& TextDropEventArgsEXT::getTextProperty() const
    {
        return text_;
    }
}
