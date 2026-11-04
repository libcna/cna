// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief CNAEXT base-from-member helper that owns PacketWriter's backing buffer.
     *
     * Listed before System::IO::BinaryWriter in PacketWriter's base list so the buffer
     * is fully constructed before BinaryWriter's constructor receives a pointer to it.
     */
    class CNAEXT PacketWriterStream
    {
    protected:
        /** @brief Constructs an empty backing buffer. */
        PacketWriterStream() = default;

        /**
         * @brief Constructs an empty backing buffer that has already allocated @p capacity bytes.
         *
         * @param capacity Bytes to allocate at once, as .NET's MemoryStream(int capacity) does:
         * Length stays 0 while the buffer XNA's SendData(PacketWriter) reads (GetBuffer()) is
         * already that long, so a never-written PacketWriter(n > 0) is refused for "count" and one
         * of capacity 0 for "offset". A negative value throws ArgumentOutOfRangeException, as
         * MemoryStream(int) does.
         */
        explicit PacketWriterStream(int capacity)
        {
            System::ArgumentOutOfRangeException::ThrowIfNegative(capacity, "capacity");
            // System::IO::MemoryStream has no capacity constructor; growing and truncating it keeps
            // the allocation, as MemoryStream(int) leaves GetBuffer() capacity bytes long.
            if (capacity > 0)
            {
                stream_.SetLength(capacity);
                stream_.SetLength(0);
            }
        }

        /** @brief The backing buffer passed to the BinaryWriter base class. */
        System::IO::MemoryStream stream_;
    };

    /**
     * @brief Writes primitive values and XNA math types to an in-memory packet buffer.
     */
    class PacketWriter final : private PacketWriterStream, public System::IO::BinaryWriter
    {
    public:
        /**
         * @brief Brings BinaryWriter's other Write(...) overloads into scope.
         *
         * Without this, declaring PacketWriter's own Write(Color)/Write(Matrix)/… overloads
         * would hide all of BinaryWriter's unrelated Write(...) overloads per C++ name-hiding
         * rules (C# has no equivalent hiding for overloads with new parameter types), silently
         * breaking calls like Write(intcs) or Write(bool). The inherited overloads are XNA API,
         * so this carries no CNAEXT marker (which C++ cannot attach to a using-declaration).
         */
        using System::IO::BinaryWriter::Write;

        /** @brief Constructs an empty PacketWriter over a new backing buffer. */
        PacketWriter();

        /**
         * @brief Constructs a PacketWriter over a new backing buffer of @p capacity bytes.
         *
         * @param capacity Bytes the backing buffer allocates at once, as XNA's MemoryStream(int)
         * does; see PacketWriterStream for where that is observable.
         * @throws System::ArgumentOutOfRangeException if @p capacity is negative.
         */
        explicit PacketWriter(int capacity);

        /**
         * @brief Gets the length, in bytes, of the packet buffer.
         *
         * @return The buffer length.
         */
        [[nodiscard]] int getLengthProperty() const;

        /**
         * @brief Gets the current write position within the packet buffer.
         *
         * @return The current position.
         */
        [[nodiscard]] int getPositionProperty() const;

        /**
         * @brief Sets the current write position within the packet buffer.
         *
         * @param value The new position.
         */
        void setPositionProperty(int value);

        /**
         * @brief Writes a Color value as four bytes (R, G, B, A).
         *
         * The bytes are the little-endian packed value, so PacketReader::ReadColor() is its
         * inverse.
         *
         * @param value The Color to write.
         */
        void Write(Color value);

        /**
         * @brief Writes a Matrix value as sixteen 32-bit floats, row-major.
         *
         * @param value The Matrix to write.
         */
        void Write(Matrix value);

        /**
         * @brief Writes a Quaternion value as four 32-bit floats (X, Y, Z, W).
         *
         * @param value The Quaternion to write.
         */
        void Write(Quaternion value);

        /**
         * @brief Writes a Vector2 value as two 32-bit floats (X, Y).
         *
         * @param value The Vector2 to write.
         */
        void Write(Vector2 value);

        /**
         * @brief Writes a Vector3 value as three 32-bit floats (X, Y, Z).
         *
         * @param value The Vector3 to write.
         */
        void Write(Vector3 value);

        /**
         * @brief Writes a Vector4 value as four 32-bit floats (X, Y, Z, W).
         *
         * @param value The Vector4 to write.
         */
        void Write(Vector4 value);

        /**
         * @brief Writes a 4-byte IEEE 754 single-precision float.
         *
         * @param value The float to write.
         */
        void Write(float value) override;

        /**
         * @brief Writes an 8-byte IEEE 754 double-precision float.
         *
         * @param value The double to write.
         */
        void Write(double value) override;
    };
}
