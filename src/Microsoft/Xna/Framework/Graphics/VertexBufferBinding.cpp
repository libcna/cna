// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    VertexBufferBinding::VertexBufferBinding() = default;

    VertexBufferBinding::VertexBufferBinding(VertexBuffer* vertexBuffer, int vertexOffset, int instanceFrequency)
        : vertexBuffer_(vertexBuffer)
        , vertexOffset_(vertexOffset)
        , instanceFrequency_(instanceFrequency)
    {
        System::ArgumentNullException::ThrowIfNull(vertexBuffer, "vertexBuffer");
        System::ArgumentOutOfRangeException::ThrowIfNegative(vertexOffset, "vertexOffset");
        System::ArgumentOutOfRangeException::ThrowIfNegative(
            instanceFrequency, "instanceFrequency");
    }

    VertexBuffer* VertexBufferBinding::getVertexBufferProperty() const { return vertexBuffer_; }
    int VertexBufferBinding::getVertexOffsetProperty() const { return vertexOffset_; }
    int VertexBufferBinding::getInstanceFrequencyProperty() const { return instanceFrequency_; }
}
