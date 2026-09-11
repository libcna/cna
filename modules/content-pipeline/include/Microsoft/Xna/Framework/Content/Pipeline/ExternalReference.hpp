// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentTypeName.hpp"
#include "System/ArgumentException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Resolves a reference's filename the way XNA does: an absolute path is kept, a
     *        relative one is resolved against the directory of the referencing content's
     *        source file.
     *
     * @param filename The authored filename.
     * @param relativeToContent The referencing content; its `SourceFilename` supplies the base.
     * @return The absolute path, lexically normalized.
     * @throws System::ArgumentException when @p filename is empty, or is relative while the
     *         identity has no source filename to resolve against.
     */
    /**
     * @brief Resolves a path a *source file* named, allowing for the case it was written in.
     *
     * Every `.x` and `.fbx` in the public XNA sample corpus was written on Windows, where a path
     * is matched without regard to case: Spacewar's `asteroid1.x` names
     * `..\textures\asteroid1.tga` and the directory beside it is called `Textures`. XNA's own
     * build resolves that; on a case-sensitive filesystem the exact spelling is simply not there,
     * and the model failed to build at all (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-115`).
     *
     * The fallback is deliberately narrow and deterministic: it applies only when the authored
     * spelling does not exist, it matches one path component at a time, and a directory holding
     * two entries that differ only in case resolves to neither -- the authored path comes back
     * unchanged so the build refuses with the name the file actually gave.
     *
     * @param directory The directory the name is relative to.
     * @param named The path as the source file spells it; a backslash is a separator.
     * @return The resolved path, or the authored one when nothing matches.
     */
    CNAEXT [[nodiscard]] std::filesystem::path ResolveNamedSourceFileEXT(
        const std::filesystem::path& directory, const std::string& named);

    CNAEXT [[nodiscard]] std::string ResolveExternalReferenceFilename(
        const std::string& filename, const ContentIdentity& relativeToContent);

    /**
     * @brief Specifies external references to a data file for the content item.
     *
     * The type parameter names the runtime type the referenced asset compiles to; the reference
     * itself carries only a filename. Instances are `sealed` in XNA, so the class is `final`.
     *
     * @tparam T The type of the referenced content.
     */
    template<typename T>
    class ExternalReference final : public ContentItem
    {
    public:
        /** @brief .NET full name of this generic instantiation. */
        CNAEXT static const std::string XnaTypeName;

        /** @brief Initializes an empty reference. */
        ExternalReference() = default;

        /**
         * @brief Initializes a reference to an absolute or already-resolved filename.
         *
         * @param filename The file the reference points at.
         */
        explicit ExternalReference(std::string filename) : filename_(std::move(filename)) {}

        /**
         * @brief Initializes a reference to a file named relative to another content item's
         *        source.
         *
         * @param filename The authored filename, absolute or relative.
         * @param relativeToContent The content item whose source directory resolves a relative name.
         */
        ExternalReference(const std::string& filename, const ContentIdentity& relativeToContent)
            : filename_(ResolveExternalReferenceFilename(filename, relativeToContent))
        {
        }

        /**
         * @brief Gets the file name of the referenced asset.
         *
         * @return The filename; empty for a default-constructed reference.
         */
        [[nodiscard]] const std::string& getFilenameProperty() const noexcept { return filename_; }

        /**
         * @brief Sets the file name of the referenced asset.
         *
         * @param value The filename.
         */
        void setFilenameProperty(std::string value) { filename_ = std::move(value); }

        /** @brief Returns the .NET full name of this instantiation. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override { return XnaTypeName; }

    private:
        std::string filename_;
    };

    template<typename T>
    const std::string ExternalReference<T>::XnaTypeName =
        "Microsoft.Xna.Framework.Content.Pipeline.ExternalReference`1[[" + ContentTypeName<T>::Name() + "]]";

    /** @brief The name trait for an `ExternalReference<T>`, spelled as a generic instantiation. */
    template<typename T>
    struct CNAEXT ContentTypeName<ExternalReference<T>>
    {
        /** @brief Returns the generic instantiation's full name. */
        [[nodiscard]] static std::string Name() { return ExternalReference<T>::XnaTypeName; }
    };
}
