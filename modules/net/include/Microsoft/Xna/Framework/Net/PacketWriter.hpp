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
     * @brief NOXNA base-from-member helper that owns PacketWriter's backing buffer.
     *
     * Listed before System::IO::BinaryWriter in PacketWriter's base list so the buffer
     * is fully constructed before BinaryWriter's constructor receives a pointer to it.
     */
    class NOXNA PacketWriterStream
    {
    protected:
        /** @brief Constructs an empty backing buffer. */
        PacketWriterStream() = default;

        /**
         * @brief Constructs an empty backing buffer.
         *
         * @param capacity Preallocation hint, otherwise ignored: System::IO::MemoryStream has no
         * capacity-preallocation constructor, and a non-negative capacity is purely a hint in .NET
         * with no effect on observable behavior. A negative value is not ignorable, though - real
         * .NET's MemoryStream(int capacity) throws ArgumentOutOfRangeException for it, so that part
         * of the contract is preserved even though preallocation itself is not.
         */
        explicit PacketWriterStream(int capacity)
        {
            System::ArgumentOutOfRangeException::ThrowIfNegative(capacity, "capacity");
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
         * NOXNA: without this, declaring PacketWriter's own Write(Color)/Write(Matrix)/…
         * overloads would hide all of BinaryWriter's unrelated Write(...) overloads per
         * C++ name-hiding rules (C# has no equivalent hiding for overloads with new
         * parameter types), silently breaking calls like Write(intcs) or Write(bool).
         */
        NOXNA using System::IO::BinaryWriter::Write;

        /** @brief Constructs an empty PacketWriter over a new backing buffer. */
        PacketWriter();

        /**
         * @brief Constructs a PacketWriter over a new backing buffer.
         *
         * @param capacity Initial capacity hint; see PacketWriterStream for why it is not
         * modeled by the underlying buffer.
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
         * Not the inverse of PacketReader::ReadColor(), which reads four floats: the
         * upstream implementation writes bytes here and reads floats there. Preserved
         * as-is for behavioral fidelity rather than symmetrized.
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
