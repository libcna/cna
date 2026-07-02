// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/IO/BinaryReader.hpp"
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
     * @brief NOXNA base-from-member helper that owns PacketReader's backing buffer.
     *
     * Listed before System::IO::BinaryReader in PacketReader's base list so the buffer
     * is fully constructed before BinaryReader's constructor receives a pointer to it.
     */
    class NOXNA PacketReaderStream
    {
    protected:
        /** @brief Constructs an empty backing buffer. */
        PacketReaderStream() = default;

        /**
         * @brief Constructs an empty backing buffer.
         *
         * @param capacity Ignored: System::IO::MemoryStream has no capacity-preallocation
         * constructor. Capacity is purely a preallocation hint in .NET with no effect on
         * observable behavior, so discarding it here is not a behavioral deviation.
         */
        explicit PacketReaderStream(int capacity) { (void) capacity; }

        /** @brief The backing buffer passed to the BinaryReader base class. */
        System::IO::MemoryStream stream_;
    };

    /**
     * @brief Reads primitive values and XNA math types from an in-memory packet buffer.
     */
    class PacketReader final : private PacketReaderStream, public System::IO::BinaryReader
    {
    public:
        /** @brief Constructs an empty PacketReader over a new backing buffer. */
        PacketReader();

        /**
         * @brief Constructs a PacketReader over a new backing buffer.
         *
         * @param capacity Initial capacity hint; see PacketReaderStream for why it is not
         * modeled by the underlying buffer.
         */
        explicit PacketReader(int capacity);

        /**
         * @brief Gets the length, in bytes, of the packet buffer.
         *
         * @return The buffer length.
         */
        [[nodiscard]] int getLengthProperty() const;

        /**
         * @brief Gets the current read position within the packet buffer.
         *
         * @return The current position.
         */
        [[nodiscard]] int getPositionProperty() const;

        /**
         * @brief Sets the current read position within the packet buffer.
         *
         * @param value The new position.
         */
        void setPositionProperty(int value);

        /**
         * @brief Reads a Color value as four 32-bit floats (R, G, B, A).
         *
         * Not the inverse of PacketWriter::Write(Color), which writes four bytes: the
         * upstream implementation reads floats here and writes bytes there. Preserved
         * as-is for behavioral fidelity rather than symmetrized.
         *
         * @return The Color read from the buffer.
         */
        [[nodiscard]] Color ReadColor();

        /**
         * @brief Reads a Matrix value as sixteen 32-bit floats, row-major.
         *
         * @return The Matrix read from the buffer.
         */
        [[nodiscard]] Matrix ReadMatrix();

        /**
         * @brief Reads a Quaternion value as four 32-bit floats (X, Y, Z, W).
         *
         * @return The Quaternion read from the buffer.
         */
        [[nodiscard]] Quaternion ReadQuaternion();

        /**
         * @brief Reads a Vector2 value as two 32-bit floats (X, Y).
         *
         * @return The Vector2 read from the buffer.
         */
        [[nodiscard]] Vector2 ReadVector2();

        /**
         * @brief Reads a Vector3 value as three 32-bit floats (X, Y, Z).
         *
         * @return The Vector3 read from the buffer.
         */
        [[nodiscard]] Vector3 ReadVector3();

        /**
         * @brief Reads a Vector4 value as four 32-bit floats (X, Y, Z, W).
         *
         * @return The Vector4 read from the buffer.
         */
        [[nodiscard]] Vector4 ReadVector4();

        /**
         * @brief Reads a 4-byte IEEE 754 single-precision float.
         *
         * @return The float read from the buffer.
         */
        float ReadSingle() override;

        /**
         * @brief Reads an 8-byte IEEE 754 double-precision float.
         *
         * @return The double read from the buffer.
         */
        double ReadDouble() override;
    };
}
