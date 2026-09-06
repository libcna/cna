// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "System/IDisposable.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads a DirectX `.x` file into the `NodeContent` graph a `ModelProcessor` consumes.
     *
     * What the genuine importer does with each construct is measured rather than assumed
     * (`tests/reference/xna40/model/model-import-oracle.json`, cases `x/*`), and the measurements
     * settled several things that could not be guessed:
     *
     * * A `.x` file is left-handed and the pipeline is right-handed, so **Z is negated** -- on
     *   positions, on normals, and on a frame's matrix as the basis change `S M S` with
     *   `S = diag(1, 1, -1)`. Triangle winding and texture coordinates are left alone.
     * * A file whose single top-level object is a `Frame` answers **that frame as the root**; any
     *   other shape answers an unnamed root with the objects as its children.
     * * Each material in a `MeshMaterialList` answers **its own `GeometryContent`**, sharing the
     *   mesh's positions through the position indices.
     * * A tick is `1 / AnimTicksPerSecond` of a second, **defaulting to 4800**, and an
     *   `AnimationContent`'s `Duration` is the last keyframe's time **truncated to whole
     *   milliseconds** while the keyframes keep their full precision.
     * * Where the file declares a skeleton (an `XSkinMeshHeader`), every animation in a set lands
     *   on the **skeleton's root bone** as one `AnimationContent` with a channel per target;
     *   where it does not, each animation lands on the node it names.
     */
    class XImporter final : public ContentImporter<Graphics::NodeContent>, public System::IDisposable
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.XImporter";

        /** @brief Initializes a new importer. */
        XImporter() = default;

        /** @brief Releases the importer. */
        ~XImporter() override;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.x` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The node graph, whose root is the file's own frame where it has exactly one.
         * @throws InvalidContentException when the file is not there or cannot be read, carrying
         *         the D3DX code XNA's own message carries for that kind of failure.
         */
        [[nodiscard]] std::shared_ptr<Graphics::NodeContent> Import(
            const std::string& filename, ContentImporterContext& context) override;

        /** @brief Releases the importer; calling it again does nothing. */
        void Dispose() override;

        /**
         * @brief The descriptor XNA declares on this importer: `.x`, processed by
         *        `ModelProcessor`, with its imported data cached.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

    protected:
        /**
         * @brief Releases the importer's resources.
         *
         * @param disposing True when called from `Dispose`, false from the destructor.
         */
        void Dispose(bool disposing);

    private:
        bool disposed_ = false;
    };
}
