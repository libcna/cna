// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"

#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

namespace Microsoft::Xna::Framework::Content
{
    ContentReader::ContentReader(
        ContentManager* manager,
        System::IO::Stream* stream,
        std::string assetName,
        int version,
        char platform,
        RecordDisposableFn recordDisposableObject,
        const CNA::Internal::Xnb::XnbReadLimits& limits)
        : System::IO::BinaryReader(stream)
        , contentManager_(manager)
        , assetName_(std::move(assetName))
        , version_(version)
        , platform_(platform)
        , recordDisposableObject_(std::move(recordDisposableObject))
        , limits_(limits)
    {
    }

    Matrix ContentReader::ReadMatrix()
    {
        Matrix result;
        result.M11 = ReadSingle(); result.M12 = ReadSingle(); result.M13 = ReadSingle(); result.M14 = ReadSingle();
        result.M21 = ReadSingle(); result.M22 = ReadSingle(); result.M23 = ReadSingle(); result.M24 = ReadSingle();
        result.M31 = ReadSingle(); result.M32 = ReadSingle(); result.M33 = ReadSingle(); result.M34 = ReadSingle();
        result.M41 = ReadSingle(); result.M42 = ReadSingle(); result.M43 = ReadSingle(); result.M44 = ReadSingle();
        return result;
    }

    Quaternion ContentReader::ReadQuaternion()
    {
        const float x = ReadSingle();
        const float y = ReadSingle();
        const float z = ReadSingle();
        const float w = ReadSingle();
        return Quaternion(x, y, z, w);
    }

    Vector2 ContentReader::ReadVector2()
    {
        Vector2 result;
        result.X = ReadSingle();
        result.Y = ReadSingle();
        return result;
    }

    Vector3 ContentReader::ReadVector3()
    {
        Vector3 result;
        result.X = ReadSingle();
        result.Y = ReadSingle();
        result.Z = ReadSingle();
        return result;
    }

    Vector4 ContentReader::ReadVector4()
    {
        Vector4 result;
        result.X = ReadSingle();
        result.Y = ReadSingle();
        result.Z = ReadSingle();
        result.W = ReadSingle();
        return result;
    }

    Color ContentReader::ReadColor()
    {
        const auto r = ReadByte();
        const auto g = ReadByte();
        const auto b = ReadByte();
        const auto a = ReadByte();
        return Color(r, g, b, a);
    }

    BoundingSphere ContentReader::ReadBoundingSphere()
    {
        const Vector3 center = ReadVector3();
        const float radius = ReadSingle();
        return BoundingSphere(center, radius);
    }

    void ContentReader::InitializeTypeReaders()
    {
        const auto table = CNA::Internal::Xnb::ParseXnbTypeReaderTable(*this, assetName_, limits_);

        typeReaders_.clear();
        typeReaders_.reserve(table.size());
        for (const auto& entry : table)
        {
            auto reader = ContentTypeReaderManager::CreateReader(entry.normalizedName);
            if (!reader)
            {
                throw ContentLoadException(
                    "'" + assetName_ + "' references an unregistered .xnb content type reader '" +
                    entry.normalizedName + "'.");
            }
            if (!reader->SupportsVersion(entry.version))
            {
                throw ContentLoadException(
                    "'" + assetName_ + "' uses reader '" + entry.normalizedName +
                    "' at an unsupported version (" + std::to_string(entry.version) + ").");
            }
            typeReaders_.push_back(std::move(reader));
        }

        // Two-pass, matching FNA's own LoadAssetReaders: every reader for this file is created
        // first, then Initialize() runs on each one, so a reader's Initialize() can safely assume
        // every other reader in the same file already exists.
        ContentTypeReaderManager managerInstance;
        for (auto& reader : typeReaders_)
        {
            reader->Initialize(managerInstance);
        }

        sharedResourceCount_ = Read7BitEncodedInt();
        if (sharedResourceCount_ < 0 || sharedResourceCount_ > limits_.maxSharedResourceCount)
        {
            throw ContentLoadException(
                "'" + assetName_ + "' has an invalid shared-resource count (" +
                std::to_string(sharedResourceCount_) + ").");
        }
        sharedResources_.assign(static_cast<std::size_t>(sharedResourceCount_), std::any{});
        sharedResourceFixups_.assign(static_cast<std::size_t>(sharedResourceCount_), {});
    }

    std::any ContentReader::InnerReadObjectAny()
    {
        const int32_t typeReaderIndex = Read7BitEncodedInt();
        if (typeReaderIndex == 0)
        {
            return std::any{};
        }
        if (typeReaderIndex < 0 || static_cast<std::size_t>(typeReaderIndex) > typeReaders_.size())
        {
            throw ContentLoadException(
                "'" + assetName_ + "' has an incorrect type reader index.");
        }
        ContentTypeReaderBase& typeReader = *typeReaders_[static_cast<std::size_t>(typeReaderIndex - 1)];
        return typeReader.ReadUntyped(*this, std::any{});
    }

    void ContentReader::ReadSharedResources()
    {
        // Read every shared resource first, matching FNA's own comment: "we have to read _all_
        // the objects first, BEFORE doing fixups".
        for (int32_t i = 0; i < sharedResourceCount_; ++i)
        {
            sharedResources_[static_cast<std::size_t>(i)] = InnerReadObjectAny();
        }

        for (int32_t i = 0; i < sharedResourceCount_; ++i)
        {
            const auto& resource = sharedResources_[static_cast<std::size_t>(i)];
            for (auto& fixup : sharedResourceFixups_[static_cast<std::size_t>(i)])
            {
                fixup(resource);
            }
        }
    }
}
