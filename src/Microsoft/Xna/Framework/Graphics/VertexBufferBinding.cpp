// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    VertexBufferBinding::VertexBufferBinding() = default;

    VertexBufferBinding::VertexBufferBinding(VertexBuffer* vertexBuffer, int vertexOffset, int instanceFrequency)
        : vertexBuffer_(vertexBuffer)
        , vertexOffset_(vertexOffset)
        , instanceFrequency_(instanceFrequency)
    {
    }

    VertexBuffer* VertexBufferBinding::getVertexBufferProperty() const { return vertexBuffer_; }
    int VertexBufferBinding::getVertexOffsetProperty() const { return vertexOffset_; }
    int VertexBufferBinding::getInstanceFrequencyProperty() const { return instanceFrequency_; }
}
