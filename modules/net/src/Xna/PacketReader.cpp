// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"

namespace Microsoft::Xna::Framework::Net
{
    PacketReader::PacketReader()
        : PacketReaderStream()
        , BinaryReader(&this->PacketReaderStream::stream_)
    {
    }

    PacketReader::PacketReader(int capacity)
        : PacketReaderStream(capacity)
        , BinaryReader(&this->PacketReaderStream::stream_)
    {
    }

    int PacketReader::getLengthProperty() const
    {
        return getBaseStreamProperty()->getLengthProperty();
    }

    int PacketReader::getPositionProperty() const
    {
        return getBaseStreamProperty()->getPositionProperty();
    }

    void PacketReader::setPositionProperty(int value)
    {
        getBaseStreamProperty()->setPositionProperty(value);
    }

    Color PacketReader::ReadColor()
    {
        // Four bytes, because PacketWriter::Write(Color) writes four: it writes the channels
        // themselves, which Color stores as bytes. Reading four floats consumed sixteen bytes for
        // a colour that occupies four, so a packet holding one could not be read back at all --
        // a 4-, 8- or 12-byte packet answered "attempted to read past the end of the stream", and
        // only a 16-byte one succeeded, with a value that was never a colour. The two are meant
        // to be inverses.
        const SharpRuntime::bytecs r = ReadByte();
        const SharpRuntime::bytecs g = ReadByte();
        const SharpRuntime::bytecs b = ReadByte();
        const SharpRuntime::bytecs a = ReadByte();
        return Color(
            static_cast<SharpRuntime::intcs>(r),
            static_cast<SharpRuntime::intcs>(g),
            static_cast<SharpRuntime::intcs>(b),
            static_cast<SharpRuntime::intcs>(a));
    }

    Matrix PacketReader::ReadMatrix()
    {
        float m11 = ReadSingle();
        float m12 = ReadSingle();
        float m13 = ReadSingle();
        float m14 = ReadSingle();
        float m21 = ReadSingle();
        float m22 = ReadSingle();
        float m23 = ReadSingle();
        float m24 = ReadSingle();
        float m31 = ReadSingle();
        float m32 = ReadSingle();
        float m33 = ReadSingle();
        float m34 = ReadSingle();
        float m41 = ReadSingle();
        float m42 = ReadSingle();
        float m43 = ReadSingle();
        float m44 = ReadSingle();
        return Matrix(
            m11, m12, m13, m14,
            m21, m22, m23, m24,
            m31, m32, m33, m34,
            m41, m42, m43, m44
        );
    }

    Quaternion PacketReader::ReadQuaternion()
    {
        float x = ReadSingle();
        float y = ReadSingle();
        float z = ReadSingle();
        float w = ReadSingle();
        return Quaternion(x, y, z, w);
    }

    Vector2 PacketReader::ReadVector2()
    {
        float x = ReadSingle();
        float y = ReadSingle();
        return Vector2(x, y);
    }

    Vector3 PacketReader::ReadVector3()
    {
        float x = ReadSingle();
        float y = ReadSingle();
        float z = ReadSingle();
        return Vector3(x, y, z);
    }

    Vector4 PacketReader::ReadVector4()
    {
        float x = ReadSingle();
        float y = ReadSingle();
        float z = ReadSingle();
        float w = ReadSingle();
        return Vector4(x, y, z, w);
    }

    float PacketReader::ReadSingle()
    {
        // No-op override kept for API-surface parity; does not alter BinaryReader::ReadSingle()'s behavior.
        return BinaryReader::ReadSingle();
    }

    double PacketReader::ReadDouble()
    {
        // No-op override kept for API-surface parity; does not alter BinaryReader::ReadDouble()'s behavior.
        return BinaryReader::ReadDouble();
    }
}
