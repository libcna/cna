// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

namespace Microsoft::Xna::Framework::Net
{
    PacketWriter::PacketWriter()
        : PacketWriterStream()
        , BinaryWriter(&this->PacketWriterStream::stream_)
    {
    }

    PacketWriter::PacketWriter(int capacity)
        : PacketWriterStream(capacity)
        , BinaryWriter(&this->PacketWriterStream::stream_)
    {
    }

    int PacketWriter::getLengthProperty() const
    {
        return getBaseStreamProperty()->getLengthProperty();
    }

    int PacketWriter::getPositionProperty() const
    {
        return getBaseStreamProperty()->getPositionProperty();
    }

    void PacketWriter::setPositionProperty(int value)
    {
        getBaseStreamProperty()->setPositionProperty(value);
    }

    void PacketWriter::Write(Color value)
    {
        BinaryWriter::Write(value.getRProperty());
        BinaryWriter::Write(value.getGProperty());
        BinaryWriter::Write(value.getBProperty());
        BinaryWriter::Write(value.getAProperty());
    }

    void PacketWriter::Write(Matrix value)
    {
        BinaryWriter::Write(value.M11);
        BinaryWriter::Write(value.M12);
        BinaryWriter::Write(value.M13);
        BinaryWriter::Write(value.M14);
        BinaryWriter::Write(value.M21);
        BinaryWriter::Write(value.M22);
        BinaryWriter::Write(value.M23);
        BinaryWriter::Write(value.M24);
        BinaryWriter::Write(value.M31);
        BinaryWriter::Write(value.M32);
        BinaryWriter::Write(value.M33);
        BinaryWriter::Write(value.M34);
        BinaryWriter::Write(value.M41);
        BinaryWriter::Write(value.M42);
        BinaryWriter::Write(value.M43);
        BinaryWriter::Write(value.M44);
    }

    void PacketWriter::Write(Quaternion value)
    {
        BinaryWriter::Write(value.X);
        BinaryWriter::Write(value.Y);
        BinaryWriter::Write(value.Z);
        BinaryWriter::Write(value.W);
    }

    void PacketWriter::Write(Vector2 value)
    {
        BinaryWriter::Write(value.X);
        BinaryWriter::Write(value.Y);
    }

    void PacketWriter::Write(Vector3 value)
    {
        BinaryWriter::Write(value.X);
        BinaryWriter::Write(value.Y);
        BinaryWriter::Write(value.Z);
    }

    void PacketWriter::Write(Vector4 value)
    {
        BinaryWriter::Write(value.X);
        BinaryWriter::Write(value.Y);
        BinaryWriter::Write(value.Z);
        BinaryWriter::Write(value.W);
    }

    void PacketWriter::Write(float value)
    {
        // No-op override kept for API-surface parity; does not alter BinaryWriter::Write(float)'s behavior.
        BinaryWriter::Write(value);
    }

    void PacketWriter::Write(double value)
    {
        // No-op override kept for API-surface parity; does not alter BinaryWriter::Write(double)'s behavior.
        BinaryWriter::Write(value);
    }
}
