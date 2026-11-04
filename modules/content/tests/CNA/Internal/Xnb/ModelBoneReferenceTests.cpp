// SPDX-License-Identifier: MS-PL
//
// TQ-018: the bone references of a runtime .xnb Model -- each bone's parent and children, each
// mesh's parent bone and the root -- built byte by byte for a two-bone, one-mesh model (two bones
// < 255, so every reference is one byte: 0 = none, n = bone n - 1).
//
// Oracle: Microsoft.Xna.Framework.Graphics.dll Model.ReadBoneReference IL_0000-0034 reads the
// reference and returns bones.get_Item(n - 1) on ReadOnlyCollection<ModelBone>, which throws
// ArgumentOutOfRangeException for an index outside the bones. Its callers: Model.ReadBones IL_0056
// (parent) and IL_0077 (each child), Model.ReadMeshes IL_001c (mesh parent) and Model.Read
// IL_0017 (root). FNA's ModelReader indexes a List<ModelBone> the same way.

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;

namespace
{
    using Bytes = std::vector<std::uint8_t>;

    struct BoneReferences
    {
        std::uint32_t rootChildCount = 1;
        std::uint8_t childOfRoot = 2;   // bone 0 lists bone 1 as its child
        std::uint8_t parentOfChild = 1; // bone 1's parent is bone 0
        std::uint8_t meshParent = 1;
        std::uint8_t root = 1;
    };

    Bytes TwoBoneModel(const BoneReferences& references)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("Microsoft.Xna.Framework.Content.ModelReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write(std::string("Microsoft.Xna.Framework.Content.StringReader"));
        writer.Write(static_cast<std::int32_t>(0));
        writer.Write7BitEncodedInt(0); // no shared resources
        writer.Write7BitEncodedInt(1); // root: the model
        writer.Write(static_cast<std::uint32_t>(2));
        for (const char* name : {"Root", "Child"})
        {
            writer.Write7BitEncodedInt(2);
            writer.Write(std::string(name));
            for (int i = 0; i < 16; ++i) { writer.Write(i % 5 == 0 ? 1.0f : 0.0f); }
        }
        writer.Write(static_cast<std::uint8_t>(0)); // bone 0: no parent
        writer.Write(references.rootChildCount);
        if (references.rootChildCount != 0) { writer.Write(references.childOfRoot); }
        writer.Write(references.parentOfChild);     // bone 1
        writer.Write(static_cast<std::uint32_t>(0));
        writer.Write(static_cast<std::int32_t>(1)); // one mesh
        writer.Write7BitEncodedInt(2);
        writer.Write(std::string("Mesh"));
        writer.Write(references.meshParent);
        for (const float value : {0.0f, 0.0f, 0.0f, 1.0f}) { writer.Write(value); }
        writer.Write7BitEncodedInt(0);              // mesh Tag
        writer.Write(static_cast<std::int32_t>(0)); // no mesh parts
        writer.Write(references.root);
        writer.Write7BitEncodedInt(0);              // model Tag
        writer.Flush();
        const auto body = stream.ToArray();
        Bytes file{'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
        const auto total = static_cast<std::uint32_t>(10 + body.size());
        for (int i = 0; i < 4; ++i) { file[6 + i] = static_cast<std::uint8_t>(total >> (8 * i)); }
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    class InMemoryContentManager final : public ContentManager
    {
    public:
        InMemoryContentManager() : ContentManager(nullptr, "Content") { RouteLoadsThroughOpenStreamEXT(true); }

        std::map<std::string, Bytes> files;

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string& assetName) override
        {
            const auto found = files.find(assetName);
            if (found == files.end()) { throw ContentLoadException("no asset '" + assetName + "'"); }
            return std::make_unique<System::IO::MemoryStream>(
                found->second.data(), static_cast<std::int32_t>(found->second.size()), false);
        }
    };

    class ModelBoneReferenceTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            manager.setGraphicsDevice(device);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice device;
        InMemoryContentManager manager;
    };
}

TEST_F(ModelBoneReferenceTest, WellFormedTwoBoneModelResolvesEveryReference)
{
    manager.files["model"] = TwoBoneModel({});
    const Model model = manager.Load<Model>("model");
    const auto& bones = model.getBonesProperty();
    ASSERT_EQ(bones.getCountProperty(), 2);
    EXPECT_EQ(bones[0]->getParentProperty(), nullptr);
    EXPECT_EQ(bones[1]->getParentProperty(), bones[0]);
    EXPECT_EQ(bones[0]->getChildrenProperty().getCountProperty(), 1);
    EXPECT_EQ(model.getRootProperty(), bones[0]);
    ASSERT_EQ(model.getMeshesProperty().getCountProperty(), 1);
    EXPECT_EQ(model.getMeshesProperty()[0]->getParentBoneProperty(), bones[0]);
}

TEST_F(ModelBoneReferenceTest, ReferenceOutsideTheBonesIsArgumentOutOfRangeWhereverItIs)
{
    // Reference 3 names bone index 2 of two bones.
    BoneReferences parent;
    parent.parentOfChild = 3;  // ReadBones IL_0056 -- accepted silently before the fix
    BoneReferences child;
    child.childOfRoot = 3;     // ReadBones IL_0077 -- ContentLoadException before the fix
    BoneReferences mesh;
    mesh.meshParent = 3;       // ReadMeshes IL_001c -- ContentLoadException before the fix
    BoneReferences root;
    root.root = 3;             // Model.Read IL_0017 -- ContentLoadException before the fix
    const std::array<std::pair<const char*, BoneReferences>, 4> cases{
        {{"parent", parent}, {"child", child}, {"mesh", mesh}, {"root", root}}};
    for (const auto& [name, references] : cases)
    {
        SCOPED_TRACE(name);
        // One asset name per case, so a model wrongly accepted (and cached) by one case cannot
        // answer for the next.
        const std::string asset = std::string("model-") + name;
        manager.files[asset] = TwoBoneModel(references);
        EXPECT_THROW((void)manager.Load<Model>(asset), System::ArgumentOutOfRangeException);
        // A refusal caches nothing: the well-formed model under the same name then loads.
        manager.files[asset] = TwoBoneModel({});
        EXPECT_EQ(manager.Load<Model>(asset).getBonesProperty().getCountProperty(), 2);
    }
}
