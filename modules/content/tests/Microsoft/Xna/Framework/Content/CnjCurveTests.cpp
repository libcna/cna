// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnj.md CNB-44/CNB-46: first-ever .cnj coverage for Curve. Self-contained JSON port of
// CurveContentTypeReader.hpp's already-FNA-verified field shape (preLoop/postLoop + keys), not a
// new design -- see that .xnb-side reader for the binary equivalent of this same field list.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

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
            : dir_(std::filesystem::temp_directory_path()
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

    ContentManager cm(nullptr, root.path().string());

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

    ContentManager cm(nullptr, root.path().string());

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

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<Curve>("empty"), ContentLoadException);
}

TEST_F(CnjCurveTest, UnrecognizedLoopTypeThrows)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "bad.cnj", R"({
        "cnjVersion": 1, "type": "Curve", "preLoop": "NotARealLoopType", "keys": []
    })");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<Curve>("bad"), ContentLoadException);
}

TEST_F(CnjCurveTest, MismatchedTypeThrowsContentLoadException)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj", R"({"cnjVersion": 1, "type": "Model"})");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<Curve>("wrong"), ContentLoadException);
}

TEST_F(CnjCurveTest, SourceFileRejected)
{
    ScratchContentRoot root;
    WriteFile(root.path() / "wrong.cnj",
              R"({"cnjVersion": 1, "type": "Curve", "sourceFile": "foo.bin"})");

    ContentManager cm(nullptr, root.path().string());

    EXPECT_THROW(cm.Load<Curve>("wrong"), ContentLoadException);
}
