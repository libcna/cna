// SPDX-License-Identifier: MS-PL
//
// CNJ CNB-44/CNB-46: first-ever .cnj coverage for Curve. Self-contained JSON port of
// CurveContentTypeReader.hpp's already-FNA-verified field shape (preLoop/postLoop + keys), not a
// new design -- see that .xnb-side reader for the binary equivalent of this same field list.

#include "CNA/TestSupport/TestScratch.hpp"
#include "CNA/TestSupport/EmptyServiceProvider.hpp"
#include <filesystem>
#include "CNA/Internal/PathUtf8.hpp"
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"

using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    class ScratchContentRoot
    {
    public:
        ScratchContentRoot()
            : dir_(CNA::TestSupport::FixtureRoot()
                   / ("cna_cnj_curve_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }

        ~ScratchContentRoot()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }

        ScratchContentRoot(const ScratchContentRoot&) = delete;
        ScratchContentRoot& operator=(const ScratchContentRoot&) = delete;

        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }
}

class CnjCurveTest : public ::testing::Test
{
};

TEST_F(CnjCurveTest, LoadsRealCnjFixture)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "fade.cnj", R"({
        "cnjVersion": 1,
        "type": "Curve",
        "preLoop": "Cycle",
        "postLoop": "Oscillate",
        "keys": [
            { "position": 0.0, "value": 0.0, "tangentIn": 0.0, "tangentOut": 1.0, "continuity": "Smooth" },
            { "position": 1.0, "value": 10.0, "tangentIn": 1.0, "tangentOut": 0.0, "continuity": "Step" }
        ]
    })");

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));

    Curve curve = cm.Load<Curve>("fade");

    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Cycle);
    EXPECT_EQ(curve.getPostLoopProperty(), CurveLoopType::Oscillate);
    ASSERT_EQ(curve.getKeysProperty().getCountProperty(), 2);

    EXPECT_FLOAT_EQ(curve.getKeysProperty()[0].getPositionProperty(), 0.0f);
    EXPECT_FLOAT_EQ(curve.getKeysProperty()[0].getValueProperty(), 0.0f);
    EXPECT_FLOAT_EQ(curve.getKeysProperty()[0].getTangentOutProperty(), 1.0f);
    EXPECT_EQ(curve.getKeysProperty()[0].getContinuityProperty(), CurveContinuity::Smooth);

    EXPECT_FLOAT_EQ(curve.getKeysProperty()[1].getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(curve.getKeysProperty()[1].getValueProperty(), 10.0f);
    EXPECT_EQ(curve.getKeysProperty()[1].getContinuityProperty(), CurveContinuity::Step);
}

TEST_F(CnjCurveTest, DefaultsAppliedWhenFieldsOmitted)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "simple.cnj", R"({
        "cnjVersion": 1,
        "type": "Curve",
        "keys": [ { "position": 0.5, "value": 2.0 } ]
    })");

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));

    Curve curve = cm.Load<Curve>("simple");

    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Constant);
    EXPECT_EQ(curve.getPostLoopProperty(), CurveLoopType::Constant);
    ASSERT_EQ(curve.getKeysProperty().getCountProperty(), 1);
    EXPECT_FLOAT_EQ(curve.getKeysProperty()[0].getTangentInProperty(), 0.0f);
    EXPECT_FLOAT_EQ(curve.getKeysProperty()[0].getTangentOutProperty(), 0.0f);
    EXPECT_EQ(curve.getKeysProperty()[0].getContinuityProperty(), CurveContinuity::Smooth);
}

TEST_F(CnjCurveTest, MissingKeysThrows)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "empty.cnj", R"({"cnjVersion": 1, "type": "Curve"})");

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));

    EXPECT_THROW(cm.Load<Curve>("empty"), ContentLoadException);
}

TEST_F(CnjCurveTest, UnrecognizedLoopTypeThrows)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "bad.cnj", R"({
        "cnjVersion": 1, "type": "Curve", "preLoop": "NotARealLoopType", "keys": []
    })");

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));

    EXPECT_THROW(cm.Load<Curve>("bad"), ContentLoadException);
}

TEST_F(CnjCurveTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj", R"({"cnjVersion": 1, "type": "Model"})");

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));

    EXPECT_THROW(cm.Load<Curve>("wrong"), ContentLoadException);
}

TEST_F(CnjCurveTest, SourceFileRejected)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "Curve", "sourceFile": "foo.bin"})");

    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));

    EXPECT_THROW(cm.Load<Curve>("wrong"), ContentLoadException);
}


TEST_F(CnjCurveTest, Safe002StructuredTypesAndDuplicateKeysAgreeAndRecover)
{
    namespace Cnb = CNA::Content::Cnb;
    ScratchContentRoot root;
    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));
    const auto path = root.path() / "candidate.cnj";
    const std::string valid = R"({"cnjVersion":1,"type":"Curve","keys":[{"position":0,"value":7}]})";
    for (const auto bad : {
             R"({"cnjVersion":-1,"type":"Curve","keys":[]})",
             R"({"cnjVersion":1e300,"type":"Curve","keys":[]})",
             R"({"cnjVersion":1.5,"type":"Curve","keys":[]})",
             R"({"cnjVersion":1,"type":"Curve","keys":{}})",
             R"({"cnjVersion":1,"type":"Curve","keys":[false]})",
             R"({"cnjVersion":1,"type":"Curve","keys":[{"position":[],"value":1}]})"})
    {
        SCOPED_TRACE(bad);
        WriteFile(path, bad);
        EXPECT_THROW((void)cm.Load<Curve>("candidate"), ContentLoadException);
        EXPECT_THROW((void)Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path)), ContentLoadException);
        WriteFile(path, valid);
        const auto recovered = cm.Load<Curve>("candidate");
        ASSERT_EQ(recovered.getKeysProperty().getCountProperty(), 1);
        EXPECT_FLOAT_EQ(recovered.getKeysProperty()[0].getValueProperty(), 7.0f);
        const auto compiled = Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path));
        const auto decoded = Cnb::DecodeCurveFromCnb(Cnb::CnbDocument::Parse(compiled.bytes, "safe002.cnb"));
        EXPECT_FLOAT_EQ(decoded.getKeysProperty()[0].getValueProperty(), 7.0f);
        cm.Unload();
    }
    // JsonValue::FindMember resolves the first occurrence, including escaped equivalent keys.
    WriteFile(path, R"({"cnjVersion":1,"cnjVersion":false,"type":"Curve","type":"Model",
                        "keys":[{"position":0,"value":7,"val\u0075e":99}]})");
    const auto duplicate = cm.Load<Curve>("candidate");
    EXPECT_FLOAT_EQ(duplicate.getKeysProperty()[0].getValueProperty(), 7.0f);
    const auto compiled = Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path));
    EXPECT_FLOAT_EQ(Cnb::DecodeCurveFromCnb(Cnb::CnbDocument::Parse(compiled.bytes, "safe002.cnb"))
                        .getKeysProperty()[0].getValueProperty(), 7.0f);
}

TEST_F(CnjCurveTest, Safe002AnimationTimeExtremesRefuseAsContentErrorsAndRecover)
{
    namespace Cnb = CNA::Content::Cnb;
    using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
    ScratchContentRoot root;
    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));
    const auto path = root.path() / "clip.cnj";
    const std::string valid = R"({"cnjVersion":1,"type":"AnimationClip","duration":1,
                                 "tracks":[{"boneIndex":0,"keys":[{"time":0}]}]})";
    for (const bool keyTime : {false, true})
    {
        for (const auto token : {"1e300", "-1e300", "1e400", "null", "[]"})
        {
            SCOPED_TRACE(keyTime);
            SCOPED_TRACE(token);
            std::string bad = valid;
            const std::string field = keyTime ? "\"time\":0" : "\"duration\":1";
            const auto at = bad.find(field);
            ASSERT_NE(at, std::string::npos);
            bad.replace(at, field.size(), (keyTime ? "\"time\":" : "\"duration\":") + std::string(token));
            WriteFile(path, bad);
            EXPECT_THROW((void)cm.Load<AnimationClipEXT>("clip"), ContentLoadException);
            EXPECT_THROW((void)Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path)), ContentLoadException);
            WriteFile(path, valid);
            const auto recovered = cm.Load<AnimationClipEXT>("clip");
            EXPECT_DOUBLE_EQ(recovered.Duration.getTotalSecondsProperty(), 1.0);
            ASSERT_EQ(recovered.Tracks.size(), 1u);
            EXPECT_EQ(recovered.Tracks[0].Keys.size(), 1u);
            const auto compiled = Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path));
            EXPECT_EQ(Cnb::DecodeAnimationClipFromCnb(Cnb::CnbDocument::Parse(compiled.bytes, "safe002.cnb"))
                          .Tracks[0].Keys.size(), 1u);
            cm.Unload();
        }
    }
}

TEST_F(CnjCurveTest, Safe002DeepDocumentsRefuseThroughBothRoutesAndRecover)
{
    namespace Cnb = CNA::Content::Cnb;
    ScratchContentRoot root;
    ContentManager cm(CNA::TestSupport::EmptyServices(), CNA::Internal::PathToUtf8(root.path()));
    const auto path = root.path() / "curve.cnj";
    const std::string prefix = R"({"cnjVersion":1,"type":"Curve","keys":[{"position":0,"value":7}],"unused":)";
    for (const std::size_t depth : {128u, 16384u})
    {
        WriteFile(path, prefix + std::string(depth, '[') + "0" + std::string(depth, ']') + "}");
        EXPECT_THROW((void)cm.Load<Curve>("curve"), ContentLoadException);
        EXPECT_THROW((void)Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path)), ContentLoadException);
        WriteFile(path, prefix + "null}");
        const auto recovered = cm.Load<Curve>("curve");
        ASSERT_EQ(recovered.getKeysProperty().getCountProperty(), 1);
        EXPECT_FLOAT_EQ(recovered.getKeysProperty()[0].getValueProperty(), 7.0f);
        const auto compiled = Cnb::CompileCnjToCnb(CNA::Internal::PathToUtf8(path));
        EXPECT_EQ(Cnb::DecodeCurveFromCnb(Cnb::CnbDocument::Parse(compiled.bytes, "recovery.cnb"))
                      .getKeysProperty().getCountProperty(), 1);
        cm.Unload();
    }
}
