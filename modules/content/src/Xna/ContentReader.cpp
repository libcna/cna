// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"

#include <algorithm>
#include <filesystem>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/IO/EndOfStreamException.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        // Mirrors FNA's MonoGame.Utilities.FileHelpers.ResolveRelativePath: resolves
        // relativeFile as a sibling of filePath (i.e. relative to filePath's containing
        // directory), then collapses "."/".." segments. Both the inputs and the result are
        // logical asset names (forward-slash-separated, relative to ContentManager's root), not
        // filesystem paths -- std::filesystem::path is used purely for its segment-collapsing
        // logic, never touching the real filesystem.
        std::string ResolveRelativeAssetPath(const std::string& filePath, const std::string& relativeFile)
        {
            namespace fs = std::filesystem;

            auto normalizeSeparators = [](std::string s)
            {
                std::replace(s.begin(), s.end(), '\\', '/');
                return s;
            };

            const std::string normalizedRelativeFile = normalizeSeparators(relativeFile);

            // REMED-CONTENT-002/XNB-35 hardening, no FNA equivalent (FNA just lets the OS fail to
            // find an escaping path): reject a reference that is itself absolute outright, rather
            // than attempting to load whatever happens to be there. This method's own doc comment
            // already promised such paths are "rejected outright"; previously only the ".."-
            // escaping case (below) was actually enforced -- an absolute reference (e.g.
            // "/etc/passwd") passed straight through unchanged, since fs::path::operator/ silently
            // discards `base` for an absolute right-hand operand.
            //
            // Deliberately NOT ResolveContainedPath(base, ...): that function's containment root is
            // the same directory the string is joined onto, but here the join base is the CURRENT
            // asset's own directory while the containment root is the content root above it -- a
            // legitimate sibling reference like "../textures/foo" from "effects/myeffect" climbs
            // out of "effects/" by design and must not be rejected just for containing "..".
            if (CNA::Internal::IsDisallowedAbsolutePath(normalizedRelativeFile))
            {
                throw ContentLoadException(
                    "ContentReader::ReadExternalReference(): '" + relativeFile + "' (relative to '" +
                    filePath + "') resolves outside the content root.");
            }

            const fs::path base = fs::path(normalizeSeparators(filePath)).parent_path();
            const fs::path combined = base / normalizedRelativeFile;
            const std::string resolved = combined.lexically_normal().generic_string();

            // Reject a reference that climbs above the content root's own logical space outright.
            if (resolved == ".." || resolved.rfind("../", 0) == 0)
            {
                throw ContentLoadException(
                    "ContentReader::ReadExternalReference(): '" + relativeFile + "' (relative to '" +
                    filePath + "') resolves outside the content root.");
            }
            return resolved;
        }
    }

    template <typename T>
    std::optional<T> ContentReader::ReadExternalReference()
    {
        const std::string externalReference = ReadString();
        if (externalReference.empty())
        {
            return std::nullopt;
        }
        if (!contentManager_)
        {
            throw ContentLoadException(
                "'" + assetName_ + "' references external asset '" + externalReference +
                "', but this ContentReader has no owning ContentManager to load it through.");
        }
        const std::string resolved = ResolveRelativeAssetPath(assetName_, externalReference);
        return contentManager_->Load<T>(resolved);
    }

    template std::optional<Graphics::Texture2D> ContentReader::ReadExternalReference<Graphics::Texture2D>();
    template std::optional<Graphics::TextureCube> ContentReader::ReadExternalReference<Graphics::TextureCube>();
    // An EffectMaterial names its compiled effect by external reference; the asset loads at
    // the same erased shared_ptr<Effect> every effect reader targets.
    template std::optional<std::shared_ptr<Graphics::Effect>>
        ContentReader::ReadExternalReference<std::shared_ptr<Graphics::Effect>>();

    std::any ContentReader::ReadExternalReference()
    {
        const std::string externalReference = ReadString();
        if (externalReference.empty())
        {
            return {};
        }
        if (!contentManager_)
        {
            throw ContentLoadException(
                "'" + assetName_ + "' references external asset '" + externalReference +
                "', but this ContentReader has no owning ContentManager to load it through.");
        }
        const std::string resolved = ResolveRelativeAssetPath(assetName_, externalReference);
        return contentManager_->LoadUntypedXnbReference(resolved);
    }

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
        , rendererThreadContextLease_(
              manager != nullptr && manager->graphicsDevice_ != nullptr
                  ? manager->graphicsDevice_->AcquireRendererThreadContextLease()
                  : nullptr)
        , assetName_(std::move(assetName))
        , version_(version)
        , platform_(platform)
        , recordDisposableObject_(std::move(recordDisposableObject))
        , limits_(limits)
    {
    }

    ContentReader::~ContentReader() = default;

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

    void ContentReader::CheckCollectionElementCount(int64_t count, const std::string& readerName) const
    {
        if (count < 0 || count > limits_.maxCollectionElementCount)
        {
            throw ContentLoadException(
                "'" + assetName_ + "': " + readerName + " declares an invalid element count (" +
                std::to_string(count) + ").");
        }
    }

    void ContentReader::CheckDecodedByteSize(int64_t byteSize, const std::string& readerName) const
    {
        if (byteSize < 0 || byteSize > limits_.maxDecompressedSize)
        {
            throw ContentLoadException(
                "'" + assetName_ + "': " + readerName + " declares dimensions requiring an "
                "invalid decoded byte size (" + std::to_string(byteSize) + ").");
        }
    }

    std::vector<uint8_t> ContentReader::ReadBytesExactOrThrow(int32_t count, const std::string& readerName)
    {
        if (count < 0)
        {
            throw ContentLoadException(
                "'" + assetName_ + "': " + readerName + " declares a negative byte count (" +
                std::to_string(count) + ").");
        }
        std::vector<uint8_t> data = ReadBytes(count);
        if (data.size() != static_cast<std::size_t>(count))
        {
            throw System::IO::EndOfStreamException(
                "'" + assetName_ + "': " + readerName + " declared " + std::to_string(count) +
                " bytes but the stream ended after " + std::to_string(data.size()) + ".");
        }
        return data;
    }

    void ContentReader::InitializeTypeReaders()
    {
        typeReaderTable_ =
            CNA::Internal::Xnb::ParseXnbTypeReaderTable(*this, assetName_, limits_);

        typeReaders_.clear();
        typeReaders_.reserve(typeReaderTable_.size());
        for (const auto& entry : typeReaderTable_)
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

    void ContentReader::InitializeCanonicalTypeReadersEXT()
    {
        typeReaderTable_ =
            CNA::Internal::Xnb::ParseXnbTypeReaderTable(*this, assetName_, limits_);
        typeReaders_.clear();

        sharedResourceCount_ = Read7BitEncodedInt();
        if (sharedResourceCount_ < 0 || sharedResourceCount_ > limits_.maxSharedResourceCount)
        {
            throw ContentLoadException(
                "'" + assetName_ + "' has an invalid shared-resource count (" +
                std::to_string(sharedResourceCount_) + ").");
        }
        sharedResources_.clear();
        sharedResourceFixups_.clear();
    }

    const CNA::Internal::Xnb::XnbTypeReaderTableEntry*
    ContentReader::ReadOptionalCanonicalTypeReaderReferenceEXT()
    {
        const int32_t typeReaderIndex = Read7BitEncodedInt();
        if (typeReaderIndex == 0)
        {
            return nullptr;
        }
        if (typeReaderIndex < 0 ||
            static_cast<std::size_t>(typeReaderIndex) > typeReaderTable_.size())
        {
            throw ContentLoadException(
                "'" + assetName_ + "' has an incorrect type reader index.");
        }
        return &typeReaderTable_[static_cast<std::size_t>(typeReaderIndex - 1)];
    }

    const CNA::Internal::Xnb::XnbTypeReaderTableEntry&
    ContentReader::ReadCanonicalTypeReaderReferenceEXT()
    {
        const auto* result = ReadOptionalCanonicalTypeReaderReferenceEXT();
        if (result == nullptr)
        {
            throw ContentLoadException(
                "'" + assetName_ + "' has a null object where canonical XNB data is required.");
        }
        return *result;
    }

    std::size_t ContentReader::getCanonicalTypeReaderCountEXT() const
    {
        return typeReaderTable_.size();
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
