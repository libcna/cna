// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class VertexBuffer;

    /// Binds a vertex buffer with an offset and instance frequency for rendering.
    class VertexBufferBinding
    {
    public:
        VertexBufferBinding();
        explicit VertexBufferBinding(VertexBuffer* vertexBuffer,
                                     int vertexOffset      = 0,
                                     int instanceFrequency = 0);

        [[nodiscard]] VertexBuffer* getVertexBufferProperty() const;
        [[nodiscard]] int getVertexOffsetProperty() const;
        [[nodiscard]] int getInstanceFrequencyProperty() const;

    private:
        VertexBuffer* vertexBuffer_ = nullptr;
        int vertexOffset_           = 0;
        int instanceFrequency_      = 0;
    };
}
