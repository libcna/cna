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

    /**
     * @brief Reads an Autodesk FBX file into the `NodeContent` graph a `ModelProcessor` consumes.
     *
     * Measured against the genuine importer over a corpus written for this repository
     * (`tests/reference/xna40/model/model-import-oracle.json`, cases `fbx/*`). What it settled,
     * and what makes FBX and `.x` different in ways nothing would predict:
     *
     * * FBX is already right-handed, so **no coordinate is converted** -- positions and normals
     *   pass through as written, where the `.x` route negates Z.
     * * **Triangle winding is reversed**: a polygon `0, 1, 2` answers indices `2, 1, 0`.
     * * **A texture coordinate's V is flipped**: `0.2` answers `0.8`.
     * * The channel order is normals, then texture coordinates, then colours -- the `.x` route's
     *   order is normals, colours, then texture coordinates.
     * * Vertex colours are **not** quantized through eight bits, where the `.x` route's are.
     * * A material reaches a batch only through a `LayerElementMaterial`, and it keeps the name
     *   the file gave it, where a `.x` material's name is dropped.
     *
     * One recorded divergence, deliberately in CNA's favour: XNA's importer carries FBX SDK
     * 2011.3.1 and refuses any document of version 7400 or above, which is every FBX a current
     * tool writes. CNA reads those too. Matching a bundled SDK's age would serve nobody, and the
     * refusal is measured and recorded rather than assumed away.
     */
    class FbxImporter final : public ContentImporter<Graphics::NodeContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.FbxImporter";

        /** @brief Initializes a new importer. */
        FbxImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the `.fbx` source.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return The node graph, whose root is the file's own model where it has exactly one.
         * @throws System::IO::FileNotFoundException when the file does not exist, with XNA's own
         *         sentence naming the path.
         * @throws InvalidContentException when the file is not a readable FBX, with the sentence
         *         XNA gives for that kind of failure.
         */
        [[nodiscard]] std::shared_ptr<Graphics::NodeContent> Import(
            const std::string& filename, ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: `.fbx`, processed by
         *        `ModelProcessor`, with its imported data cached.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
