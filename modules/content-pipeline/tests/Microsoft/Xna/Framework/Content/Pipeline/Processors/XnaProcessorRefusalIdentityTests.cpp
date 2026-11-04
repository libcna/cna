// SPDX-License-Identifier: MS-PL
//
// TQ-034: a processor refusal names the content it refuses. MeshHelper.CalculateTangentFrames throws
// its missing-channel refusal under the mesh's identity (IL_0076-007c), and
// FontRasterizer.CreateFont its unknown-family refusal under the description's (IL_0122-0128).
// Measured: TQ-034 lane oracle model/tangent_frames_no_uv_identity and font/unknown_family_identity.
// CNA threw both with an empty identity, so the build error could not say which file.
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "XnaProcessorTestContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/FontDescription.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/FontProcessors.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace
{
    namespace Pipeline = Microsoft::Xna::Framework::Content::Pipeline;
    namespace Graphics = Microsoft::Xna::Framework::Content::Pipeline::Graphics;
    using CNA::Tests::XnaProcessors::RecordingContext;
    using CNA::Tests::XnaProcessors::RefusalOf;
    using CNA::Tests::XnaProcessors::Spell;
    using Microsoft::Xna::Framework::Vector3;
}

TEST(XnaProcessorRefusalIdentity, AMissingTangentFrameChannelNamesTheMesh)
{
    auto mesh = std::make_shared<Graphics::MeshContent>();
    mesh->setNameProperty("Mesh");
    mesh->setIdentityProperty(Pipeline::ContentIdentity("mesh.x", "XImporter", "mesh"));
    mesh->getPositionsProperty().Add(Vector3(0, 0, 0));
    mesh->getPositionsProperty().Add(Vector3(1, 0, 0));
    mesh->getPositionsProperty().Add(Vector3(0, 1, 0));
    auto geometry = std::make_shared<Graphics::GeometryContent>();
    mesh->getGeometryProperty().Add(geometry);
    geometry->setIdentityProperty(Pipeline::ContentIdentity("geometry.x", "XImporter", "geometry"));
    geometry->getVerticesProperty().AddRange({0, 1, 2});
    geometry->getIndicesProperty().AddRange({0, 1, 2});
    geometry->getVerticesProperty().getChannelsProperty().Add<Vector3>(
        Graphics::VertexChannelNames::Normal(), {Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)});
    auto root = std::make_shared<Graphics::NodeContent>();
    root->getChildrenProperty().Add(mesh);

    RecordingContext context;
    const auto refusal = RefusalOf([&]
    {
        Pipeline::Processors::ModelProcessor processor;
        processor.setGenerateTangentFramesProperty(true);
        (void)processor.Process(root, context);
    });
    EXPECT_EQ(refusal.message, "Required vertex channel \"TextureCoordinate0\" not found.");
    EXPECT_EQ(Spell(refusal.identity), "{mesh.x|XImporter|mesh}");

    // The same refusal straight from the helper.
    const auto direct = RefusalOf([&]
    {
        Graphics::MeshHelper::CalculateTangentFrames(mesh, Graphics::VertexChannelNames::TextureCoordinate(0),
                                                     Graphics::VertexChannelNames::Tangent(0),
                                                     Graphics::VertexChannelNames::Binormal(0));
    });
    EXPECT_EQ(Spell(direct.identity), "{mesh.x|XImporter|mesh}");
}

TEST(XnaProcessorRefusalIdentity, AnUnknownFontFamilyNamesTheDescription)
{
    // One character, so CNA reaches the family lookup: XNA looks the family up before it counts
    // characters (FontDescriptionProcessor.Process IL_006e-0073), CNA after (an open difference
    // recorded in TQ-034's DESIGN.md, D-12).
    auto description = std::make_shared<Graphics::FontDescription>("NoSuchFontFamilyTq034", 12.0f, 0.0f);
    description->getCharactersProperty().insert(u'A');
    description->setIdentityProperty(
        Pipeline::ContentIdentity("missing.spritefont", "FontDescriptionImporter", "font"));
    RecordingContext context;
    const auto refusal = RefusalOf([&]
    {
        Pipeline::Processors::FontDescriptionProcessor processor;
        (void)processor.Process(description, context);
    });
    EXPECT_EQ(refusal.message, "The font family \"NoSuchFontFamilyTq034\" could not be found. Please ensure the "
                               "requested font is installed, and is a TrueType or OpenType font.");
    EXPECT_EQ(Spell(refusal.identity), "{missing.spritefont|FontDescriptionImporter|font}");
}
