// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class VertexBuffer;

    /** @brief Binds a vertex buffer with an offset and instance frequency for rendering. */
    class VertexBufferBinding
    {
    public:
        /** @brief Constructs a VertexBufferBinding with a null vertex buffer. */
        VertexBufferBinding();
        /**
         * @brief Constructs a VertexBufferBinding with the given buffer, offset, and frequency.
         * @param vertexBuffer      The vertex buffer to bind.
         * @param vertexOffset      Offset in vertices from the start of the buffer.
         * @param instanceFrequency Number of instances to draw per element; 0 means no instancing.
         * @throws System::ArgumentNullException if @p vertexBuffer is null.
         * @throws System::ArgumentOutOfRangeException if @p vertexOffset or
         *         @p instanceFrequency is negative.
         */
        explicit VertexBufferBinding(VertexBuffer* vertexBuffer,
                                     int vertexOffset      = 0,
                                     int instanceFrequency = 0);

        /** @brief Returns the bound vertex buffer. */
        [[nodiscard]] VertexBuffer* getVertexBufferProperty() const;
        /** @brief Returns the offset (in vertices) from the start of the buffer. */
        [[nodiscard]] int getVertexOffsetProperty() const;
        /** @brief Returns the instance frequency (0 = no instancing). */
        [[nodiscard]] int getInstanceFrequencyProperty() const;

    private:
        VertexBuffer* vertexBuffer_ = nullptr;
        int vertexOffset_           = 0;
        int instanceFrequency_      = 0;
    };
}
