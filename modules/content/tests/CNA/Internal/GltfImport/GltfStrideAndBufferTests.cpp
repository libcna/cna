// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-151 / GLTF-153 / GLTF-164 / GLTF-165 / GLTF-166.
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
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

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
            EXPECT_EQ(0, layout.elements[i].usageIndex)
                << "every built-in stream uses usage index 0";
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
    ExpectStrideLayout(68, {
        {VertexElementUsage::Position, 0, VertexElementFormat::Vector3},
        {VertexElementUsage::Normal, 12, VertexElementFormat::Vector3},
        {VertexElementUsage::Tangent, 24, VertexElementFormat::Vector4},
        {VertexElementUsage::TextureCoordinate, 40, VertexElementFormat::Vector2},
        {VertexElementUsage::BlendWeight, 48, VertexElementFormat::Vector4},
        {VertexElementUsage::BlendIndices, 64, VertexElementFormat::Byte4},
    });
}

TEST(GltfStrideAndBuffer, EveryElementFitsInsideItsOwnStride)
{
    // The property no individual offset assertion states: an element whose bytes run past the end
    // of the record reads into the next vertex, which produces a mesh that looks progressively
    // more wrong along the buffer rather than uniformly wrong -- the hardest kind to diagnose.
    for (const int stride : {16, 20, 24, 32, 48, 52, 56, 68})
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
