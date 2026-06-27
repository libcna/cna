// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    std::string VertexPositionColor::ToString() const
    {
        return "{{Position:" + Position.ToString()
             + " Color:" + Color.ToString()
             + "}}";
    }
}
