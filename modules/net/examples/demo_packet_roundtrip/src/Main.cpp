#include <cstdio>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "System/IO/MemoryStream.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

// Task 15.2: cna_demo_packet_roundtrip. Console-only, single process, no networking: writes a
// random value of every XNA-type PacketWriter::Write/PacketReader::Read pair into one
// PacketWriter, moves the raw bytes into a fresh PacketReader (the same MemoryStream::ToArray() /
// Stream::Write() dance LocalNetworkGamer::SendData/ReceiveData use for a real network hop), and
// reads them back to prove each pair round-trips correctly.

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Net;

namespace
{
    std::vector<SharpRuntime::bytecs> DrainToBytes(PacketWriter& writer)
    {
        auto* mem = dynamic_cast<System::IO::MemoryStream*>(writer.getBaseStreamProperty());
        return mem != nullptr ? mem->ToArray() : std::vector<SharpRuntime::bytecs>{};
    }

    void LoadIntoReader(PacketReader& reader, const std::vector<SharpRuntime::bytecs>& bytes)
    {
        reader.setPositionProperty(0);
        reader.getBaseStreamProperty()->Write(bytes.data(), 0, static_cast<int>(bytes.size()));
        reader.setPositionProperty(0);
    }

    int gTotal = 0;
    int gPass = 0;

    void Report(const char* type, bool ok, const std::string& detail)
    {
        ++gTotal;
        if (ok)
        {
            ++gPass;
        }
        std::printf("%-10s | %-4s | %s\n", type, ok ? "PASS" : "FAIL", detail.c_str());
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::mt19937 rng(20260707u);
    std::uniform_real_distribution<float> floatDist(-1000.0f, 1000.0f);
    std::uniform_real_distribution<double> doubleDist(-1.0e6, 1.0e6);
    std::uniform_int_distribution<int> byteDist(0, 255);

    std::printf("%-10s | %-4s | %s\n", "Type", "Res.", "Detail");
    std::printf("------------------------------------------------------------------\n");

    // float
    {
        PacketWriter writer;
        const float value = floatDist(rng);
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const float back = reader.ReadSingle();
        Report("float", back == value,
               "wrote " + std::to_string(value) + ", read " + std::to_string(back));
    }

    // double
    {
        PacketWriter writer;
        const double value = doubleDist(rng);
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const double back = reader.ReadDouble();
        Report("double", back == value,
               "wrote " + std::to_string(value) + ", read " + std::to_string(back));
    }

    // Vector2
    {
        PacketWriter writer;
        const Vector2 value(floatDist(rng), floatDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const Vector2 back = reader.ReadVector2();
        Report("Vector2", back == value, "wrote " + value.ToString() + ", read " + back.ToString());
    }

    // Vector3
    {
        PacketWriter writer;
        const Vector3 value(floatDist(rng), floatDist(rng), floatDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const Vector3 back = reader.ReadVector3();
        Report("Vector3", back == value, "wrote " + value.ToString() + ", read " + back.ToString());
    }

    // Vector4
    {
        PacketWriter writer;
        const Vector4 value(floatDist(rng), floatDist(rng), floatDist(rng), floatDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const Vector4 back = reader.ReadVector4();
        Report("Vector4", back == value, "wrote " + value.ToString() + ", read " + back.ToString());
    }

    // Quaternion
    {
        PacketWriter writer;
        const Quaternion value(floatDist(rng), floatDist(rng), floatDist(rng), floatDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const Quaternion back = reader.ReadQuaternion();
        Report("Quaternion", back == value, "wrote " + value.ToString() + ", read " + back.ToString());
    }

    // Matrix
    {
        PacketWriter writer;
        const Matrix value(
            floatDist(rng), floatDist(rng), floatDist(rng), floatDist(rng),
            floatDist(rng), floatDist(rng), floatDist(rng), floatDist(rng),
            floatDist(rng), floatDist(rng), floatDist(rng), floatDist(rng),
            floatDist(rng), floatDist(rng), floatDist(rng), floatDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        const Matrix back = reader.ReadMatrix();
        Report("Matrix", back == value, "wrote " + value.ToString() + ", read " + back.ToString());
    }

    // Color's wire representation is four raw bytes (R,G,B,A). First inspect that layout
    // explicitly; the next row checks the public ReadColor() inverse.
    {
        PacketWriter writer;
        const Color value(byteDist(rng), byteDist(rng), byteDist(rng), byteDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        // Read into named locals in stream order first - C++ does not guarantee left-to-right
        // argument evaluation, so 4x inline ReadByte() calls passed directly as constructor
        // arguments would consume the stream in an unspecified (observed: reversed) order.
        const SharpRuntime::bytecs r = reader.ReadByte();
        const SharpRuntime::bytecs g = reader.ReadByte();
        const SharpRuntime::bytecs b = reader.ReadByte();
        const SharpRuntime::bytecs a = reader.ReadByte();
        const Color back(r, g, b, a);
        Report("Color", back == value, "wrote " + value.ToString() + ", read " + back.ToString() +
                                            " (wire layout via 4x ReadByte())");
    }

    // The public Color pair must round-trip exactly and consume only four bytes.
    {
        PacketWriter writer;
        const Color value(byteDist(rng), byteDist(rng), byteDist(rng), byteDist(rng));
        writer.Write(value);
        PacketReader reader;
        LoadIntoReader(reader, DrainToBytes(writer));
        try
        {
            const Color back = reader.ReadColor();
            Report("ReadColor", back == value && reader.getPositionProperty() == 4,
                   "wrote " + value.ToString() + ", read " + back.ToString());
        }
        catch (const std::exception& ex)
        {
            Report("ReadColor", false, "unexpected exception: " + std::string(ex.what()));
        }
    }

    std::printf("------------------------------------------------------------------\n");
    std::printf("%d/%d rows round-tripped as expected.\n",
                gPass, gTotal);
    return gPass == gTotal ? 0 : 1;
}
