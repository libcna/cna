// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace Microsoft::Xna::Framework::Content
{
    ContentLoadException::ContentLoadException(const std::string& message)
        : std::runtime_error(message)
    {
    }

    ContentLoadException::ContentLoadException(const std::string& message, const std::exception& inner)
        : std::runtime_error(message + std::string(" ---> ") + inner.what())
    {
    }
}
