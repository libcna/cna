// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"

// plans/plan_xnb.md XNB-18A: primitive .xnb readers. FNA's own equivalents (src/Content/
// ContentReaders/*.cs) are all `internal class` -- never subclassed or referenced directly by
// game code, only dispatched through the type-reader table -- so these live in
// CNA::Internal::Xnb rather than the public Microsoft::Xna::Framework::Content namespace,
// matching that true visibility (see ContentTypeReader.hpp's own note on the analogous
// ContentTypeReaderBase rename). Each one is registered under the real FNA canonical name (e.g.
// "Microsoft.Xna.Framework.Content.ByteReader") by RegisterPrimitiveXnbReaders(), independent of
// this C++ namespace/class name.

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;

    class BooleanReader : public ContentTypeReader<bool>
    {
    public:
        BooleanReader() : ContentTypeReader<bool>("System.Boolean") {}
    protected:
        bool Read(ContentReader& input, std::optional<bool>) override { return input.ReadBoolean(); }
    };

    class ByteReader : public ContentTypeReader<uint8_t>
    {
    public:
        ByteReader() : ContentTypeReader<uint8_t>("System.Byte") {}
    protected:
        uint8_t Read(ContentReader& input, std::optional<uint8_t>) override { return input.ReadByte(); }
    };

    class SByteReader : public ContentTypeReader<int8_t>
    {
    public:
        SByteReader() : ContentTypeReader<int8_t>("System.SByte") {}
    protected:
        int8_t Read(ContentReader& input, std::optional<int8_t>) override { return input.ReadSByte(); }
    };

    class Int16Reader : public ContentTypeReader<int16_t>
    {
    public:
        Int16Reader() : ContentTypeReader<int16_t>("System.Int16") {}
    protected:
        int16_t Read(ContentReader& input, std::optional<int16_t>) override { return input.ReadInt16(); }
    };

    class UInt16Reader : public ContentTypeReader<uint16_t>
    {
    public:
        UInt16Reader() : ContentTypeReader<uint16_t>("System.UInt16") {}
    protected:
        uint16_t Read(ContentReader& input, std::optional<uint16_t>) override { return input.ReadUInt16(); }
    };

    class Int32Reader : public ContentTypeReader<int32_t>
    {
    public:
        Int32Reader() : ContentTypeReader<int32_t>("System.Int32") {}
    protected:
        int32_t Read(ContentReader& input, std::optional<int32_t>) override { return input.ReadInt32(); }
    };

    class UInt32Reader : public ContentTypeReader<uint32_t>
    {
    public:
        UInt32Reader() : ContentTypeReader<uint32_t>("System.UInt32") {}
    protected:
        uint32_t Read(ContentReader& input, std::optional<uint32_t>) override { return input.ReadUInt32(); }
    };

    class Int64Reader : public ContentTypeReader<int64_t>
    {
    public:
        Int64Reader() : ContentTypeReader<int64_t>("System.Int64") {}
    protected:
        int64_t Read(ContentReader& input, std::optional<int64_t>) override { return input.ReadInt64(); }
    };

    class UInt64Reader : public ContentTypeReader<uint64_t>
    {
    public:
        UInt64Reader() : ContentTypeReader<uint64_t>("System.UInt64") {}
    protected:
        uint64_t Read(ContentReader& input, std::optional<uint64_t>) override { return input.ReadUInt64(); }
    };

    class SingleReader : public ContentTypeReader<float>
    {
    public:
        SingleReader() : ContentTypeReader<float>("System.Single") {}
    protected:
        float Read(ContentReader& input, std::optional<float>) override { return input.ReadSingle(); }
    };

    class DoubleReader : public ContentTypeReader<double>
    {
    public:
        DoubleReader() : ContentTypeReader<double>("System.Double") {}
    protected:
        double Read(ContentReader& input, std::optional<double>) override { return input.ReadDouble(); }
    };

    class CharReader : public ContentTypeReader<char16_t>
    {
    public:
        CharReader() : ContentTypeReader<char16_t>("System.Char") {}
    protected:
        char16_t Read(ContentReader& input, std::optional<char16_t>) override { return input.ReadChar(); }
    };

    class StringReader : public ContentTypeReader<std::string>
    {
    public:
        StringReader() : ContentTypeReader<std::string>("System.String") {}
    protected:
        std::string Read(ContentReader& input, std::optional<std::string>) override { return input.ReadString(); }
    };

    /** @brief Registers all primitive readers above under their real FNA canonical names. Idempotent. */
    void RegisterPrimitiveXnbReaders();
}
