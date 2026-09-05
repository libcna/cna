// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/SpriteFontContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    const std::string& SpriteFontContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
