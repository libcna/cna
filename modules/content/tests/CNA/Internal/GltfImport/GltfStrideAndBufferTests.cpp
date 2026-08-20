// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-151 / GLTF-153 / GLTF-158 / GLTF-159 / GLTF-162 / GLTF-164 / GLTF-165 / GLTF-166.
//
// The vertex/index buffer ABI, asserted at the layer a game actually meets it: the byte offsets a
// renderer will read from, the index element size the GPU is told about, and what happens at the
// two boundaries -- 65 535 vertices, and a primitive with nothing in it.
//
// The offsets in particular are worth an explicit test rather than a re-derivation. Every renderer
// reads a glTF-imported buffer through `InferredLayoutForStride`, and the numbers in it were
// originally copied from four independent `switch (stride)` blocks. A test that recomputed them
// the same way the table does would agree with any value at all; these are written out, one
// assertion per element, so that changing the table is a deliberate act with a diff to review.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

using namespace CNA::Internal::GltfImport;
using CNA::Internal::Graphics::InferredLayoutForStride;
using CNA::Internal::Graphics::InferredVertexLayout;
using CNA::Internal::Graphics::UnlistedStrideLayout;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_stride_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    struct ExpectedElement
    {
        VertexElementUsage usage;
        int offset;
        VertexElementFormat format;
        int usageIndex = 0;
    };

    /// Asserts a stride's whole layout, element for element and in order -- so a table entry that
    /// gained, lost or reordered an element fails here rather than at whichever renderer notices
    /// its attributes have shifted.
    void ExpectStrideLayout(int stride, const std::vector<ExpectedElement>& expected)
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const InferredVertexLayout layout =
            InferredLayoutForStride(stride, UnlistedStrideLayout::RendererRefusesIt);
        ASSERT_TRUE(layout.known) << "the canonical table no longer knows this stride at all";
        ASSERT_EQ(expected.size(), layout.count) << "the element count changed";
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            SCOPED_TRACE("element " + std::to_string(i));
            EXPECT_EQ(static_cast<int>(expected[i].usage),
                      static_cast<int>(layout.elements[i].usage));
            EXPECT_EQ(expected[i].offset, layout.elements[i].offset);
            EXPECT_EQ(static_cast<int>(expected[i].format),
                      static_cast<int>(layout.elements[i].format));
            EXPECT_EQ(expected[i].usageIndex, layout.elements[i].usageIndex);
        }
    }

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
            out += kAlphabet[(chunk >> 18) & 0x3F];
            out += kAlphabet[(chunk >> 12) & 0x3F];
            out += (i + 1 < bytes.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            out += (i + 2 < bytes.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }
        return out;
    }

    /// A mesh of `vertexCount` positions with one triangle drawn from its last three vertices, so
    /// the index values exercise the top of the range rather than the bottom. Generated rather
    /// than committed: GLTF-164 needs meshes either side of 65 535 vertices, and those are 800 KB
    /// of positions apiece.
    std::string LargeMeshDocument(int vertexCount, int indexComponentType)
    {
        std::vector<std::uint8_t> buffer;
        buffer.reserve(static_cast<std::size_t>(vertexCount) * 12 + 16);
        for (int v = 0; v < vertexCount; ++v)
        {
            const float xyz[3] = {static_cast<float>(v), 0.0f, 0.0f};
            std::uint8_t bytes[12];
            std::memcpy(bytes, xyz, sizeof(bytes));
            buffer.insert(buffer.end(), bytes, bytes + 12);
        }
        const std::size_t indexOffset = buffer.size();
        const std::size_t componentBytes = indexComponentType == 5125 ? 4u : 2u;
        const std::uint32_t indices[3] = {
            static_cast<std::uint32_t>(vertexCount - 3),
            static_cast<std::uint32_t>(vertexCount - 2),
            static_cast<std::uint32_t>(vertexCount - 1),
        };
        for (const std::uint32_t index : indices)
        {
            for (std::size_t b = 0; b < componentBytes; ++b)
            {
                buffer.push_back(static_cast<std::uint8_t>((index >> (8 * b)) & 0xFF));
            }
        }
        while (buffer.size() % 4 != 0) { buffer.push_back(0); }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "BigMesh", "mesh": 0 } ],
  "meshes": [ { "name": "Big", "primitives": [
    { "attributes": { "POSITION": 0 }, "indices": 1 } ] } ],
  "buffers": [ { "byteLength": )GLTF") + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )GLTF" +
               std::to_string(static_cast<std::size_t>(vertexCount) * 12) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(indexOffset) + R"GLTF(, "byteLength": )GLTF" +
               std::to_string(3 * componentBytes) + R"GLTF( }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": )GLTF" + std::to_string(vertexCount) +
               R"GLTF(, "type": "VEC3", "min": [0, 0, 0], "max": [)GLTF" +
               std::to_string(vertexCount - 1) + R"GLTF(, 0, 0] },
    { "bufferView": 1, "componentType": )GLTF" + std::to_string(indexComponentType) +
               R"GLTF(, "count": 3, "type": "SCALAR" }
  ]
})GLTF";
    }
}

// --- GLTF-151: every element of every stride ------------------------------------------------------

TEST(GltfStrideAndBuffer, EveryCanonicalStrideHasExactlyTheElementsSection23States)
{
    ExpectStrideLayout(16, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Color, 12, VertexElementFormat::Color},
    });
    ExpectStrideLayout(20, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::TextureCoordinate, 12, VertexElementFormat::Vector2},
    });
    ExpectStrideLayout(24, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Color, 12, VertexElementFormat::Color},
        {VertexElementUsage::TextureCoordinate, 16, VertexElementFormat::Vector2},
    });
    ExpectStrideLayout(32, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::TextureCoordinate, 24, VertexElementFormat::Vector2},
    });
    ExpectStrideLayout(48, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::Tangent, 24, VertexElementFormat::Vector4},
        {VertexElementUsage::TextureCoordinate, 40, VertexElementFormat::Vector2},
    });
    ExpectStrideLayout(52, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::TextureCoordinate, 24, VertexElementFormat::Vector2},
        {VertexElementUsage::BlendWeight, 32, VertexElementFormat::Vector4},
        {VertexElementUsage::BlendIndices, 48, VertexElementFormat::Byte4},
    });
    ExpectStrideLayout(56, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::TextureCoordinate, 24, VertexElementFormat::Vector2},
        {VertexElementUsage::BlendWeight, 32, VertexElementFormat::Vector4},
        {VertexElementUsage::BlendIndices, 48, VertexElementFormat::Byte4},
        {VertexElementUsage::Color, 52, VertexElementFormat::Color},
    });
    // plans/plan_gltf.md GLTF-462: the four bytes GLTF-182 reserved purely to keep this stride distinct
    // from 56 are the packed COLOR_0 slot now, which is what lets a vertex-coloured primitive keep
    // its metallic-roughness material instead of being downgraded to a layout with no Normal at all.
    ExpectStrideLayout(60, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::Tangent, 24, VertexElementFormat::Vector4},
        {VertexElementUsage::TextureCoordinate, 40, VertexElementFormat::Vector2, 0},
        {VertexElementUsage::TextureCoordinate, 48, VertexElementFormat::Vector2, 1},
        {VertexElementUsage::Color, 56, VertexElementFormat::Color},
    });
    ExpectStrideLayout(68, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::Tangent, 24, VertexElementFormat::Vector4},
        {VertexElementUsage::TextureCoordinate, 40, VertexElementFormat::Vector2},
        {VertexElementUsage::BlendWeight, 48, VertexElementFormat::Vector4},
        {VertexElementUsage::BlendIndices, 64, VertexElementFormat::Byte4},
    });
    ExpectStrideLayout(76, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::Tangent, 24, VertexElementFormat::Vector4},
        {VertexElementUsage::TextureCoordinate, 40, VertexElementFormat::Vector2, 0},
        {VertexElementUsage::BlendWeight, 48, VertexElementFormat::Vector4},
        {VertexElementUsage::BlendIndices, 64, VertexElementFormat::Byte4},
        {VertexElementUsage::TextureCoordinate, 68, VertexElementFormat::Vector2, 1},
    });
}

TEST(GltfStrideAndBuffer, EveryElementFitsInsideItsOwnStride)
{
    // The property no individual offset assertion states: an element whose bytes run past the end
    // of the record reads into the next vertex, which produces a mesh that looks progressively
    // more wrong along the buffer rather than uniformly wrong -- the hardest kind to diagnose.
    for (const int stride : {16, 20, 24, 32, 48, 52, 56, 60, 68, 76})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const InferredVertexLayout layout =
            InferredLayoutForStride(stride, UnlistedStrideLayout::RendererRefusesIt);
        ASSERT_TRUE(layout.known);
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            int size = 0;
            switch (layout.elements[i].format)
            {
                case VertexElementFormat::Vector2: size = 8;  break;
                case VertexElementFormat::Vector3: size = 12; break;
                case VertexElementFormat::Vector4: size = 16; break;
                case VertexElementFormat::Color:   size = 4;  break;
                case VertexElementFormat::Byte4:   size = 4;  break;
                default: FAIL() << "unhandled format in the canonical table";
            }
            EXPECT_LE(layout.elements[i].offset + size, stride)
                << "element " << i << " runs past the end of its own vertex record";
        }
    }
}

TEST(GltfStrideAndBuffer, AStrideOutsideTheTableIsNotGuessedAt)
{
    // The refusal half of the same contract, and the reason GLTF-157 exists: an unlisted stride
    // must come back unknown rather than with a plausible layout. A guessed layout binds
    // *something* at every location, and the draw then reads whatever those bytes happen to be.
    for (const int stride : {0, 12, 28, 40, 64, 72})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        EXPECT_FALSE(InferredLayoutForStride(stride, UnlistedStrideLayout::RendererRefusesIt).known);
    }
}

// --- GLTF-158/159: real declaration plus renderer draw-boundary conformance ----------------------

TEST(GltfStrideAndBuffer, EveryImportedGltfStrideCarriesItsCanonicalVertexDeclaration)
{
    // Each fixture below is the corpus witness for exactly one of the eight layouts a glTF mesh
    // can select. Loading, rather than constructing a VertexBuffer directly, is essential: the
    // regression was in BuildVertexBufferFromRawBytes, where the capacity-only constructor erased
    // the declaration before SetData/SetDataRaw reached the selected renderer.
    struct Case { const char* fixture; int stride; };
    const Case cases[] = {
        {"tex-dual-texture-stride", 20},
        // GLTF-462: a rigid vertex-coloured primitive keeps its metallic-roughness material now,
        // so it takes the rigid PBR layout and its colour rides in stride 60's own colour slot.
        // Stride 24 is reached only by a coloured primitive whose material declares
        // KHR_materials_unlit -- `mat-unlit-vertex-color-alpha` is that fixture.
        {"normalized-u8-color", 60},
        {"mat-unlit", 32},
        {"mat-authored-tangent", 48},
        {"uv1-material", 60},
        {"skin-unlit", 52},
        // GLTF-463: a SKINNED vertex-coloured primitive is the one combination still without a
        // colour-carrying PBR stride, so it keeps SkinnedEffect's stride-56 layout.
        {"skin-vertex-color", 56},
        {"skin-parented-joints", 68},
    };

    GraphicsDevice gd;
    ContentManager cm(nullptr, "tests/assets/gltf");
    cm.setGraphicsDevice(gd);

    for (const Case& c : cases)
    {
        SCOPED_TRACE(std::string(c.fixture) + " / stride " + std::to_string(c.stride));
        Model model = cm.Load<Model>(c.fixture);
        ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
        ASSERT_EQ(1, model.getMeshesProperty()[0]->getMeshPartsProperty().getCountProperty());
        const auto* part = model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
        ASSERT_NE(nullptr, part->getVertexBufferProperty());

        const auto& declaration =
            part->getVertexBufferProperty()->getVertexDeclarationProperty();
        EXPECT_EQ(c.stride, declaration.getVertexStrideProperty());

        const InferredVertexLayout canonical =
            InferredLayoutForStride(c.stride, UnlistedStrideLayout::RendererRefusesIt);
        ASSERT_TRUE(canonical.known);
        const auto& actual = declaration.GetVertexElements();
        ASSERT_EQ(canonical.count, actual.size());
        for (std::size_t i = 0; i < canonical.count; ++i)
        {
            SCOPED_TRACE("element " + std::to_string(i));
            EXPECT_EQ(canonical.elements[i].offset, actual[i].getOffsetProperty());
            EXPECT_EQ(static_cast<int>(canonical.elements[i].format),
                      static_cast<int>(actual[i].getVertexElementFormatProperty()));
            EXPECT_EQ(static_cast<int>(canonical.elements[i].usage),
                      static_cast<int>(actual[i].getVertexElementUsageProperty()));
            EXPECT_EQ(canonical.elements[i].usageIndex, actual[i].getUsageIndexProperty());
        }
    }
}

TEST(RendererStrideConformance, EveryGltfStrideReachesTheNativeDrawBoundary)
{
    // The declaration test above stops at the public VertexBuffer. This leg makes each selected
    // renderer consume the uploaded buffer in an actual indexed draw, which is where Vulkan builds
    // its pipeline input state and EasyGL checks the declaration against the selected stock
    // program. A renderer can therefore accept an upload yet still fail here because one semantic
    // is absent, misordered or has the wrong native format.
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
    {
        GTEST_SKIP() << "renderer has no native 3D draw boundary; the upload/declaration contract "
                        "is covered by EveryImportedGltfStrideCarriesItsCanonicalVertexDeclaration";
    }

    ContentManager cm(nullptr, "tests/assets/gltf");
    cm.setGraphicsDevice(gd);
    const char* fixtures[] = {
        "tex-dual-texture-stride", "normalized-u8-color", "mat-unlit",
        "mat-authored-tangent", "uv1-material", "skin-unlit", "skin-vertex-color",
        "skin-parented-joints",
    };
    const auto identity = Microsoft::Xna::Framework::Matrix::getIdentityProperty();

    for (const char* fixture : fixtures)
    {
        SCOPED_TRACE(fixture);
        Model model = cm.Load<Model>(fixture);
        // plans/plan_gltf.md GLTF-473 replaced the plain EXPECT_NO_THROW here, and it is a STRICTER
        // requirement rather than a looser one. A fixed-function renderer has no attribute-per-
        // element freedom: it binds each client array at one literal offset, so a record it was not
        // written for is not "a stride it cannot reach" -- it is a stride it reaches through the
        // wrong bytes. OPENGLES1 passed this assertion for six of these eight fixtures by drawing
        // PBR and skinned records with `glColorPointer` at offset 12, which is their NORMAL. Not
        // throwing was exactly the symptom. So the rule is now the partition: reach the boundary, or
        // refuse with a diagnostic that names the layout incompatibility. `GLTF-473` appears only in
        // the shared fixed-function guard's message, so no other renderer can satisfy this by
        // accident.
        std::string failure;
        try
        {
            model.Draw(identity, identity, identity);
            // Deferred renderers submit the draw here. Without Present, a Vulkan command can be
            // recorded but never reach the driver's input-layout validation.
            gd.Present();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
        }
        if (failure.empty()) { continue; }
        // plans/plan_gltf.md GLTF-477 adds the second legitimate refusal: a renderer with no
        // metallic-roughness shading path at all, which is a different state from a fixed-function
        // renderer misreading a layout. Both tokens come from a shared guard nobody can reproduce
        // by accident, which is the property that made the narrow check worth having.
        const bool namedRefusal = failure.find("GLTF-473") != std::string::npos ||
                                  failure.find("GLTF-477") != std::string::npos;
        EXPECT_TRUE(namedRefusal)
            << "this renderer refused a canonical glTF stride for a reason other than a named "
               "layout incompatibility or a named absent shading model: " << failure;
    }
}

TEST(RendererStrideConformance, AColourCarryingPbrPrimitiveEitherDrawsOrRefusesByName)
{
    // plans/plan_gltf.md GLTF-465, at the draw boundary rather than in shader text. The audit above it
    // asks whether a renderer DECLARES the stride-60/80 colour; this asks whether a real
    // vertex-coloured metallic-roughness Model can be drawn through it at all.
    //
    // That distinction is not academic. Both of the following shipped with a complete layout row, a
    // complete shader and a passing source-text audit, and neither could draw the asset:
    //
    //   - SDL_GPU built the stride-60/80 pipelines and left `DrawPrimitivesEx` selecting the PBR
    //     queue for `stride == 48`/`68` only, so the draw fell through to the stride-16 coloured
    //     path and was refused there as "requires a stride-16 (VertexPositionColor) vertex buffer";
    //   - DILIGENT chose `SkinnedPbrColor3D` for stride 80 and then refused it nine lines later as
    //     "needs a skinned PBR vertex layout (stride 68 or 76)".
    //
    // So the assertion is the partition itself, in the only form that can tell those two apart from
    // a real refusal: the draw either SUCCEEDS, or it fails with a diagnostic that names the vertex
    // colour. A renderer that has not implemented the product calls the shared
    // `RequireVertexColourPbrSupportEXT`, whose message says `COLOR_0` -- that is a limitation. A
    // renderer that fails for any other reason is refusing content it has the code to draw, and the
    // message a caller would have to debug points at the wrong thing entirely.
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
    {
        GTEST_SKIP() << "renderer has no native 3D draw boundary";
    }

    ContentManager cm(nullptr, "tests/assets/gltf");
    cm.setGraphicsDevice(gd);
    const auto identity = Microsoft::Xna::Framework::Matrix::getIdentityProperty();

    // The rigid stride-60 record and the skinned stride-80 one -- the only two layouts a core glTF
    // `COLOR_0` on a metallic-roughness material can import to.
    for (const char* fixture : {"mat-vertex-color-pbr", "skin-vertex-color-pbr"})
    {
        SCOPED_TRACE(fixture);
        Model model = cm.Load<Model>(fixture);
        std::string failure;
        try
        {
            model.Draw(identity, identity, identity);
            gd.Present();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
        }
        if (failure.empty()) { continue; }
        if (failure.find("COLOR_0") != std::string::npos) { continue; }

        // Two other refusals are allowed, and only two. A fixed-function renderer with no PBR path
        // at all refuses this draw by naming the exact layout incompatibility -- which semantic, at
        // which offset, where the record really keeps it, and which effect sent the draw there. That
        // is a more specific answer than "COLOR_0 is unsupported", not a vaguer one.
        //
        // `GLTF-477` is the second, and it is a STRONGER answer rather than a weaker one: the
        // renderer has no metallic-roughness shading model whatsoever, so the draw fails for the
        // material rather than for one term of it, and saying "this COLOR_0 is unsupported" would
        // imply the rest of the material was fine. `OPENGL1` is the renderer that reaches here --
        // it used to emit every record wider than 32 bytes as flat white geometry and report
        // success.
        //
        // LLGL's two "needs Texture bound" messages used to be pinned here as well: it treated
        // PbrEffect's base-colour map as mandatory, so it could not draw a `baseColorFactor`-only
        // material -- glTF's own default (§3.9.2), and what both fixtures here author. `GLTF-474`
        // removed that rule rather than the exception, so the exception is gone too. A pinned
        // allowance that can no longer fire is a place for a regression to hide.
        constexpr std::array<const char*, 2> namedPreconditions{{
            "GLTF-473", "GLTF-477",
        }};
        const bool named = std::any_of(
            namedPreconditions.begin(), namedPreconditions.end(),
            [&](const char* known) { return failure.find(known) != std::string::npos; });
        EXPECT_TRUE(named)
            << "this renderer refused a valid core glTF vertex-coloured metallic-roughness "
               "primitive without naming either the semantic it cannot honour or a precondition "
               "of its own, which is what a renderer whose PBR route never learned the "
               "colour-carrying stride looks like. The diagnostic was: "
            << failure;
    }
}

TEST(RendererStrideConformance, NoPbrOrSkinnedRecordIsEverReadThroughAnIncompatibleLayout)
{
    // plans/plan_gltf.md GLTF-473, and the test that fails on the implementation this replaces.
    //
    // OPENGLES1 has no programmable pipeline, so PbrEffect, SkinnedEffect and a custom ShaderEffect
    // are permanent gaps (docs/opengles1-renderer.md). It did not refuse those draws: it routed them
    // to its colour path, which binds `glColorPointer` at byte offset 12 -- a colour in exactly two
    // of CNA's canonical records, and the NORMAL in every PBR and skinned one. Six of the eight
    // canonical glTF fixtures below were therefore drawn on a real ES 1.1 driver with per-vertex
    // colours read out of the bytes of their own normals: accepted input, incorrect semantics.
    //
    // Nothing caught it, and the reason is worth stating: every existing assertion about these
    // fixtures was `EXPECT_NO_THROW`, and not throwing was the symptom. So this asserts the opposite
    // for the one renderer that cannot possibly render them -- the draw MUST be refused, by name.
    // On the old implementation every one of these succeeds silently and every expectation below
    // fails.
    //
    // The strides are the whole PBR and skinned family, not the stride 60 the defect was reported
    // on. A fix that special-cased stride 60 leaves 48, 52, 56, 68, 76 and 80 reading normals as
    // colours, and this would still fail.
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
    {
        GTEST_SKIP() << "renderer has no native 3D draw boundary";
    }

    ContentManager cm(nullptr, "tests/assets/gltf");
    cm.setGraphicsDevice(gd);
    const auto identity = Microsoft::Xna::Framework::Matrix::getIdentityProperty();

    // fixture -> the stride it imports to, so a layout change that moves one is a visible failure
    // here rather than a silent loss of coverage.
    struct PbrFamilyFixture { const char* name; int stride; };
    constexpr std::array<PbrFamilyFixture, 7> fixtures{{
        {"mat-authored-tangent", 48},     // rigid PBR
        {"normalized-u8-color", 60},      // rigid PBR + COLOR_0 -- the reported case
        {"uv1-material", 60},             // rigid PBR + TEXCOORD_1
        {"mat-vertex-color-pbr", 60},     // rigid PBR + COLOR_0, factor-only material
        {"skin-unlit", 52},               // skinned, unlit
        {"skin-vertex-color", 56},        // skinned + COLOR_0
        {"skin-parented-joints", 68},     // skinned PBR
    }};

    // A renderer with no fixed-function equivalent for these effects must refuse them. OPENGLES1 is
    // the only such renderer in the tree, and its own documentation calls PbrEffect and
    // SkinnedEffect permanent gaps rather than unfinished ones -- so "renders it" is not an outcome
    // it is allowed to reach, and silence is the defect.
    const bool mustRefuse = CNA::getCurrentGraphicsRendererName() == "OPENGLES1";

    for (const PbrFamilyFixture& fixture : fixtures)
    {
        SCOPED_TRACE(std::string(fixture.name) + " (stride " + std::to_string(fixture.stride) + ")");
        Model model = cm.Load<Model>(fixture.name);

        // The fixture really is the stride this row claims, so the coverage cannot rot silently.
        const auto* part = model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
        ASSERT_NE(nullptr, part->getVertexBufferProperty());
        EXPECT_EQ(fixture.stride,
                  part->getVertexBufferProperty()->getVertexDeclarationProperty()
                      .getVertexStrideProperty())
            << "this fixture no longer imports to the stride this test was written for";

        std::string failure;
        try
        {
            model.Draw(identity, identity, identity);
            gd.Present();
        }
        catch (const std::exception& error)
        {
            failure = error.what();
        }

        if (!mustRefuse)
        {
            // Everywhere else, either outcome is allowed and both are safe: a completed draw means
            // the renderer describes the record, and a thrown refusal means it declined to read it.
            // What this test forbids is the third outcome -- reading it through a layout that does
            // not describe it -- and an exception is proof that did not happen. The QUALITY of these
            // renderers' refusals is a separate, already-recorded matter: `SDL_GPU`, `LLGL` and
            // `DILIGENT` each refuse some of these fixtures for a real precondition of their own but
            // name it poorly, which is `GLTF-474`, not this row.
            continue;
        }

        // EXPECT rather than ASSERT, so a regression reports every stride it corrupts rather than
        // stopping at the first and reading like a single-layout problem.
        EXPECT_FALSE(failure.empty())
            << "this renderer has no fixed-function equivalent for this effect and cannot describe "
               "this record, yet the draw was accepted. That is the GLTF-473 defect: the vertex "
               "data is being read through a layout that does not describe it, and the result is a "
               "plausible surface reported as a successful draw.";
        if (failure.empty()) { continue; }
        EXPECT_NE(std::string::npos, failure.find("GLTF-473")) << failure;
        EXPECT_NE(std::string::npos, failure.find("OPENGLES1")) << failure;
        // The refusal must say what would have been misread, not merely that something was wrong.
        EXPECT_NE(std::string::npos, failure.find("Normal0"))
            << "offset 12 is the NORMAL in every record here; a refusal that does not say so is not "
               "actionable: " << failure;

        // And the refusal has to have happened BEFORE the renderer touched anything -- an
        // "explicit refusal" that already bound state or submitted work is not a refusal, it is a
        // half-executed draw with an exception on the end. The observable form of that rule is
        // recovery: the very next valid draw must still render. This is the same property
        // `DeclarationGuardTest.AValidDrawAfterARefusedOneStillRenders` pins for the declaration
        // guard, asserted here for the fixed-function layout guard.
        Model recovery = cm.Load<Model>("mat-unlit");
        EXPECT_NO_THROW({
            recovery.Draw(identity, identity, identity);
            gd.Present();
        }) << "a valid draw after a refused one failed, so the refusal left renderer state behind";
    }
}

// --- GLTF-153 / GLTF-164: the 65 535 boundary, through to the IndexBuffer -------------------------

TEST(GltfStrideAndBuffer, TheIndexBufferElementSizeFollowsTheVertexCountOnBothSides)
{
    // The importer's own choice is locked by GltfIndexForm; this is the half that reaches the GPU:
    // the IndexBuffer must be *created* with the matching element size. A buffer created as
    // SixteenBits over 32-bit data draws every second index as garbage, and one created as
    // ThirtyTwoBits over 16-bit data reads two indices as one -- both of which render, badly.
    struct Case { int vertexCount; int componentType; IndexElementSize expected; };
    const Case cases[] = {
        {65535, 5123, IndexElementSize::SixteenBits},
        {65536, 5125, IndexElementSize::ThirtyTwoBits},
    };

    for (const Case& c : cases)
    {
        SCOPED_TRACE(std::to_string(c.vertexCount) + " vertices");
        const ScratchDir dir;
        GraphicsDevice gd;
        ContentManager cm(nullptr, dir.path().string());
        cm.setGraphicsDevice(gd);
        std::ofstream(dir.path() / "big.gltf", std::ios::binary)
            << LargeMeshDocument(c.vertexCount, c.componentType);

        Model model = cm.Load<Model>("big");
        ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
        ASSERT_EQ(1, model.getMeshesProperty()[0]->getMeshPartsProperty().getCountProperty());
        const auto* part = model.getMeshesProperty()[0]->getMeshPartsProperty()[0];

        ASSERT_NE(nullptr, part->getIndexBufferProperty());
        EXPECT_EQ(static_cast<int>(c.expected),
                  static_cast<int>(part->getIndexBufferProperty()->getIndexElementSizeProperty()));

        // GLTF-164: and the mesh really is the size it claims -- the whole vertex range arrived,
        // and the drawn triangle addresses its very last vertex.
        ASSERT_NE(nullptr, part->getVertexBufferProperty());
        EXPECT_EQ(c.vertexCount, part->getNumVerticesProperty());
        EXPECT_EQ(1, part->getPrimitiveCountProperty());
    }
}

// --- GLTF-165 / GLTF-166: what happens when there is nothing to upload ----------------------------

TEST(GltfStrideAndBuffer, AnUnbuildableVertexBufferSurfacesAsAContentLoadExceptionRatherThanACrash)
{
    // `BuildVertexBufferFromRawBytes` used to have no `else`: a stride it did not recognise fell
    // out of the branch chain and the freshly constructed, EMPTY buffer was returned as though it
    // had been filled, so the mesh drew from whatever that object happened to contain (GLTF-157).
    // It throws now, and what this asserts is the property a caller depends on -- the failure
    // arrives as an exception at `Load`, naming the file, rather than as a crash or a silent
    // empty draw.
    //
    // The stride is reached through a real file: a COLOR_0 primitive with a metallic-roughness
    // material and a tangent is the shape whose layout selection has the most branches, so if a
    // future change lands it on an unlisted stride this is where it shows up.
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);

    // A primitive whose POSITION accessor has zero elements: nothing to upload at all.
    std::ofstream(dir.path() / "empty.gltf", std::ios::binary) << R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "EmptyNode", "mesh": 0 } ],
  "meshes": [ { "name": "Nothing", "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 4, "uri": "data:application/octet-stream;base64,AAAAAA==" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 4 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 0, "type": "VEC3" } ]
})GLTF";

    std::string message;
    try
    {
        Model model = cm.Load<Model>("empty");
        // If it loads at all, it must not have produced a zero-sized buffer (GLTF-166): a
        // zero-length GPU allocation is undefined on several backends and a draw of zero
        // primitives is at best wasted work.
        for (int mi = 0; mi < model.getMeshesProperty().getCountProperty(); ++mi)
        {
            const auto& parts = model.getMeshesProperty()[mi]->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                EXPECT_GT(parts[pi]->getNumVerticesProperty(), 0)
                    << "a part with no vertices was created rather than refused";
            }
        }
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }

    // Either outcome is acceptable to GLTF-165/166 as long as it is one of these two and not a
    // crash; what is asserted is that the failure is *named* when it happens.
    if (!message.empty())
    {
        EXPECT_NE(std::string::npos, message.find("empty"))
            << "the failure does not name the file it came from: " << message;
    }
}

TEST(GltfStrideAndBuffer, AZeroIndexPrimitiveDoesNotProduceAZeroSizedIndexBuffer)
{
    // GLTF-166's other half, reached through the path that can actually produce it: a TRIANGLES
    // primitive with two indices has no whole triangle in it, so GLTF-079 trims the run to
    // nothing. What must not follow is a zero-length IndexBuffer allocation.
    {
        cgltf_options options{};
        cgltf_data* data = nullptr;
        const std::string json = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "Degenerate", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1 } ] } ],
  "buffers": [ { "byteLength": 40, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAAA" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 4 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5123, "count": 2, "type": "SCALAR" }
  ]
})GLTF";
        ASSERT_EQ(cgltf_result_success,
                  cgltf_parse(&options, json.data(), json.size(), &data));
        ASSERT_EQ(cgltf_result_success, cgltf_load_buffers(&options, data, "."));

        const MeshOut mesh =
            ExtractMesh(data, data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
        EXPECT_TRUE(mesh.indexBytes.empty())
            << "two indices are not a triangle; the run should have been trimmed to nothing";
        EXPECT_EQ(2u, mesh.droppedIncompleteIndicesEXT) << "and the drop must be reported";
        cgltf_free(data);
    }
}

// --- GLTF-162: the SetDataRaw contract ------------------------------------------------------------

namespace
{
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

    /// One stride-32 vertex, packed exactly as the GPU stream lays it out.
    std::vector<std::uint8_t> PackedStride32(float seed)
    {
        const float values[8] = {seed, seed + 1, seed + 2,        // position
                                  0.0f, 0.0f, 1.0f,                // normal
                                  seed + 3, seed + 4};             // uv
        std::vector<std::uint8_t> bytes(32);
        std::memcpy(bytes.data(), values, sizeof(values));
        return bytes;
    }
}

TEST(GltfStrideAndBuffer, SetDataRawUploadsExactlyCountTimesStrideBytes)
{
    // `SetDataRaw` is how morph blending re-uploads a deformed vertex buffer (`SetMorphWeightsEXT`)
    // every time the weights change, and it is the one upload entry point that takes raw bytes with
    // no vertex type to check them against. Its whole contract is the arithmetic: `count * stride`
    // bytes from the pointer, laid out exactly as the GPU stream expects, with nothing inferred.
    GraphicsDevice gd;
    Microsoft::Xna::Framework::Graphics::VertexBuffer vb(gd, 2);

    std::vector<std::uint8_t> bytes = PackedStride32(10.0f);
    const std::vector<std::uint8_t> second = PackedStride32(100.0f);
    bytes.insert(bytes.end(), second.begin(), second.end());
    vb.SetDataRaw(bytes.data(), 2, 32);

    VertexPositionNormalTexture read[2];
    vb.GetData(read, 2);
    EXPECT_FLOAT_EQ(10.0f, read[0].Position.X);
    EXPECT_FLOAT_EQ(12.0f, read[0].Position.Z);
    EXPECT_FLOAT_EQ(1.0f, read[0].Normal.Z);
    EXPECT_FLOAT_EQ(13.0f, read[0].TextureCoordinate.X);
    EXPECT_FLOAT_EQ(100.0f, read[1].Position.X)
        << "the second vertex did not land at offset 32 -- the stride was not honoured";
    EXPECT_FLOAT_EQ(104.0f, read[1].TextureCoordinate.Y);
}

TEST(GltfStrideAndBuffer, SetDataRawRefusesACountOrStrideItCannotHonour)
{
    // The refusals matter more than the happy path: with no element type to validate against, a
    // caller's arithmetic mistake would otherwise be an out-of-bounds read on the way to the GPU.
    // Each case below is one a morph re-upload could actually make -- a vertex count left over
    // from before a resize, a stride taken from the wrong part, a buffer that was never filled.
    GraphicsDevice gd;
    Microsoft::Xna::Framework::Graphics::VertexBuffer vb(gd, 2);

    std::vector<std::uint8_t> bytes = PackedStride32(1.0f);
    const std::vector<std::uint8_t> second = PackedStride32(2.0f);
    bytes.insert(bytes.end(), second.begin(), second.end());
    vb.SetDataRaw(bytes.data(), 2, 32);

    VertexPositionNormalTexture before[2];
    vb.GetData(before, 2);

    // Either outcome is a refusal: throwing (which is what an over-capacity upload does, and what
    // an XNA caller expects from an out-of-range argument) or returning without writing. What is
    // asserted after all six is that not one of them changed a byte.
    const std::vector<std::uint8_t> other = PackedStride32(999.0f);
    auto refuse = [&](const void* data, int count, int stride, const char* what)
    {
        SCOPED_TRACE(what);
        try
        {
            vb.SetDataRaw(data, count, stride);
        }
        catch (const std::exception&)
        {
            // A named refusal is the better of the two acceptable outcomes.
        }
    };
    refuse(other.data(), 3, 32, "more vertices than the buffer holds");
    refuse(nullptr, 2, 32, "nothing to read from");
    refuse(other.data(), 0, 32, "no vertices");
    refuse(other.data(), -1, 32, "a negative count");
    refuse(other.data(), 2, 0, "a zero stride");
    // NOT tested as a refusal, and the reason is the contract itself: a stride wider than the
    // source is undetectable here. The buffer was created without a VertexDeclaration, so it has
    // a capacity in VERTICES and none in bytes -- `SetDataRaw(p, 2, 48)` is a legal request to
    // read 96 bytes, and whether `p` has 96 bytes behind it is the caller's promise. That is what
    // makes `count * stride` the whole contract, and why the morph re-upload path computes both
    // from the same MeshOut rather than from two places.

    VertexPositionNormalTexture after[2];
    vb.GetData(after, 2);
    for (int i = 0; i < 2; ++i)
    {
        SCOPED_TRACE("vertex " + std::to_string(i));
        EXPECT_FLOAT_EQ(before[i].Position.X, after[i].Position.X)
            << "a refused upload wrote to the buffer anyway";
        EXPECT_FLOAT_EQ(before[i].TextureCoordinate.Y, after[i].TextureCoordinate.Y);
    }
}
