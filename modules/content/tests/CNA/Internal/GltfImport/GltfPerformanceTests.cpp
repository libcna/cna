// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md Phase 22 (`GLTF-433` … `GLTF-443`): the import path's cost and lifetime, measured.
//
// Every row in that phase accepts "measured; fixed or documented with numbers", and the numbers are
// the point: "a GLB is re-parsed on every load" is a design fact, not a defect, and whether it
// matters is a question only a measurement answers. So each test here records what it measured
// (`RecordProperty`, visible in `--gtest_output=xml`) and asserts a **ceiling generous enough that
// only a pathological regression trips it** -- an assertion tuned to today's number would be a
// flake generator on a loaded machine, and a benchmark that fails at random gets deleted.
//
// The three that are not timings are the important ones. Two are *structural* facts a stopwatch
// cannot see -- whether a second load shares anything with the first, and whether a Model's copy
// shares its owned resources -- and one is a leak, which is measured by running this file in the
// sanitizer build (`docs/gltf-conformance.md`), not by anything asserted here.
//
// `docs/gltf-performance.md` carries the numbers and the decision each row reached.

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::CorpusFixtureIds;
using CnaTest::GltfOracle::LoadedFixture;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;

namespace
{
    /// Wall-clock milliseconds for one call of @p work, averaged over @p iterations.
    template <typename Work>
    double MillisecondsPerCall(int iterations, Work&& work)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) { work(); }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::milli>(elapsed).count() / iterations;
    }

    /// A fixture with a material, a texture-free PBR path and real geometry -- the ordinary case.
    constexpr const char* kOrdinary = "mat-factor-only-gold";
}

// --- GLTF-433: what one Load<Model> costs, and what it repeats ----------------------------------

TEST(GltfPerformance, ParseCostAndCachedCostAreMeasuredSeparately)
{
    // GLTF-433's premise -- "a GLB is fully re-parsed on every call" -- is **false as stated**, and
    // the measurement is what said so: `ContentManager` caches by asset name, which is XNA's own
    // documented contract for `Load<T>`. The re-parse happens once per manager per name, not once
    // per call. Both numbers are recorded, because they answer different questions: the first is
    // what a level load costs, the second is what a `Load` in a game loop costs.
    GraphicsDevice gd;

    constexpr int kIterations = 50;
    const double msPerParse = MillisecondsPerCall(kIterations, [&] {
        ContentManager fresh(nullptr, CorpusDirectory().string());
        fresh.setGraphicsDevice(gd);
        Model model = fresh.Load<Model>(kOrdinary);
        (void)model;
    });

    ContentManager warm(nullptr, CorpusDirectory().string());
    warm.setGraphicsDevice(gd);
    Model primed = warm.Load<Model>(kOrdinary);
    (void)primed;
    const double msPerCachedLoad = MillisecondsPerCall(kIterations, [&] {
        Model model = warm.Load<Model>(kOrdinary);
        (void)model;
    });

    RecordProperty("msPerParse_x1000", static_cast<int>(msPerParse * 1000.0));
    RecordProperty("msPerCachedLoad_x1000", static_cast<int>(msPerCachedLoad * 1000.0));
    RecordProperty("iterations", kIterations);

    // Two orders of magnitude above the measured cost of a small asset: a tripwire for a
    // regression that changes the shape of the work -- an accidental O(n^2), a re-read per
    // primitive -- not a benchmark target.
    EXPECT_LT(msPerParse, 50.0)
        << "one parse-and-load of a three-vertex asset took " << msPerParse
        << " ms, which is not a slow machine -- it is a different algorithm";
    // The cache has to be worth having. A cached load is a map lookup and a copy; if it ever costs
    // as much as a parse, the cache has stopped working and nothing else would say so.
    EXPECT_LT(msPerCachedLoad, msPerParse)
        << "a cached load (" << msPerCachedLoad << " ms) costs as much as a parse (" << msPerParse
        << " ms) -- ContentManager's cache is no longer hit";
}

TEST(GltfPerformance, LoadingOneNameTwiceReturnsTheSameInstanceAndAFreshManagerDoesNot)
{
    // The structural half, and the correction this test made to the row it belongs to. XNA's
    // `ContentManager.Load<T>` returns the SAME instance for the same asset name -- so two loads
    // share their meshes, their buffers and their effects, and mutating one is mutating both. That
    // is the contract, not a defect, and a caller who wants two independent models must use two
    // managers (or `Unload` between them). Both directions are pinned here, because the first
    // draft of this test asserted the opposite and was wrong.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);

    Model first = cm.Load<Model>(kOrdinary);
    Model second = cm.Load<Model>(kOrdinary);

    auto* firstPart = first.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    auto* secondPart = second.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    ASSERT_NE(nullptr, firstPart);
    ASSERT_NE(nullptr, secondPart);
    EXPECT_EQ(firstPart, secondPart)
        << "two loads of one asset name returned different parts -- ContentManager's cache is "
           "gone, and every Load now re-parses the container";
    EXPECT_EQ(firstPart->getVertexBufferProperty(), secondPart->getVertexBufferProperty());

    auto* effect = dynamic_cast<Microsoft::Xna::Framework::Graphics::PbrEffect*>(
        firstPart->getEffectProperty());
    ASSERT_NE(nullptr, effect);
    effect->setAlphaProperty(0.125f);
    auto* alsoEffect = dynamic_cast<Microsoft::Xna::Framework::Graphics::PbrEffect*>(
        secondPart->getEffectProperty());
    ASSERT_NE(nullptr, alsoEffect);
    EXPECT_FLOAT_EQ(0.125f, alsoEffect->getAlphaProperty())
        << "the two loads did not share an effect after all";

    // A second manager shares nothing -- which is what makes "the parse happens once per manager"
    // the accurate statement of the cost, and what a caller does when they need two independent
    // copies of one asset.
    ContentManager other(nullptr, CorpusDirectory().string());
    other.setGraphicsDevice(gd);
    Model independent = other.Load<Model>(kOrdinary);
    auto* independentPart = independent.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    ASSERT_NE(nullptr, independentPart);
    EXPECT_NE(firstPart, independentPart)
        << "two ContentManagers shared a model, so the cache is global and Unload on one would "
           "free the other's buffers";
    auto* independentEffect = dynamic_cast<Microsoft::Xna::Framework::Graphics::PbrEffect*>(
        independentPart->getEffectProperty());
    ASSERT_NE(nullptr, independentEffect);
    EXPECT_NE(0.125f, independentEffect->getAlphaProperty());
}

// --- GLTF-435: how much temporary float the unpack path allocates -------------------------------

TEST(GltfPerformance, AccessorUnpackTemporaryBytesAreComputedForTheWholeCorpus)
{
    // Deliberately computed, not timed. `UnpackAccessor` expands every accessor to `float` before
    // the packing loop narrows it again, so the temporary is exactly
    // `count * components * sizeof(float)` per accessor -- an arithmetic property of the file, and
    // therefore a number that is identical on every machine and every run, unlike a duration.
    //
    // What it is for: the corpus is tiny, so this states the *ratio* rather than a total. A real
    // asset's temporary is the same multiple of its own accessor data, which is what makes the
    // number transferable: an UNSIGNED_BYTE colour stream expands 4x, a normalized short 2x, and a
    // float stream not at all.
    std::size_t storedBytes = 0;
    std::size_t temporaryBytes = 0;
    std::size_t widestExpansionAccessorBytes = 0;
    double widestExpansion = 1.0;

    for (const std::string& id : CorpusFixtureIds())
    {
        const LoadedFixture fixture(id);
        if (!fixture.Ok()) { continue; }
        const cgltf_data& data = fixture.Data();
        for (cgltf_size i = 0; i < data.accessors_count; ++i)
        {
            const cgltf_accessor& accessor = data.accessors[i];
            const std::size_t components = cgltf_num_components(accessor.type);
            const std::size_t componentSize = cgltf_component_size(accessor.component_type);
            const std::size_t stored = static_cast<std::size_t>(accessor.count) * components * componentSize;
            const std::size_t temporary = static_cast<std::size_t>(accessor.count) * components * sizeof(float);
            storedBytes += stored;
            temporaryBytes += temporary;
            if (stored > 0)
            {
                const double expansion = static_cast<double>(temporary) / static_cast<double>(stored);
                if (expansion > widestExpansion)
                {
                    widestExpansion = expansion;
                    widestExpansionAccessorBytes = stored;
                }
            }
        }
    }

    ASSERT_GT(storedBytes, 0u);
    RecordProperty("accessorStoredBytes", static_cast<int>(storedBytes));
    RecordProperty("accessorTemporaryFloatBytes", static_cast<int>(temporaryBytes));
    RecordProperty("widestExpansionRatio_x100", static_cast<int>(widestExpansion * 100.0));
    RecordProperty("widestExpansionAccessorBytes", static_cast<int>(widestExpansionAccessorBytes));

    // 4x is the worst case the format allows: a single-byte component widened to a float. Anything
    // above it means the expansion is no longer per component -- a second copy, or a wider type.
    EXPECT_LE(widestExpansion, 4.0)
        << "an accessor expands " << widestExpansion
        << "x on unpack, which is more than byte-to-float -- the temporary is no longer one float "
           "per component";
}

// --- GLTF-436 / GLTF-438: lifetime ---------------------------------------------------------------

TEST(GltfPerformance, AThousandLoadUnloadCyclesLeakNothing)
{
    // The row asks for no leak over 1000 cycles under ASan/LSan, and that is precisely where the
    // assertion lives: this test allocates and releases the whole object graph 1000 times, and the
    // sanitizer build is what turns a leak into a failure. In an ordinary build it is still worth
    // running -- it is the only test that exercises ContentManager::Unload in a loop at all.
    constexpr int kCycles = 1000;
    GraphicsDevice gd;

    const double msPerCycle = MillisecondsPerCall(kCycles, [&] {
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(kOrdinary);
        ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
        cm.Unload();
    });
    RecordProperty("cycles", kCycles);
    RecordProperty("msPerLoadUnloadCycle_x1000", static_cast<int>(msPerCycle * 1000.0));
    EXPECT_LT(msPerCycle, 50.0) << "a load/unload cycle costs " << msPerCycle << " ms";
}

TEST(GltfPerformance, UnloadThenLoadAgainYieldsAWorkingModel)
{
    // GLTF-438's observable half. Whether `Unload` released everything is a question for the
    // sanitizer; what a caller can check is that the manager is still usable afterwards and that
    // the model it hands back is a real one rather than a husk pointing at freed buffers.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);

    {
        Model before = cm.Load<Model>(kOrdinary);
        ASSERT_EQ(1, before.getMeshesProperty().getCountProperty());
    }
    cm.Unload();

    Model after = cm.Load<Model>(kOrdinary);
    ASSERT_EQ(1, after.getMeshesProperty().getCountProperty());
    auto* part = after.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    ASSERT_NE(nullptr, part);
    EXPECT_NE(nullptr, part->getEffectProperty()) << "the reloaded part has no effect";
    EXPECT_NE(nullptr, part->getVertexBufferProperty()) << "the reloaded part has no vertex buffer";
    EXPECT_GT(part->getPrimitiveCountProperty(), 0);
}

// --- GLTF-440: what copying a Model shares --------------------------------------------------------

TEST(GltfPerformance, ACopiedModelSharesItsOwnedResourcesAndOutlivesTheOriginal)
{
    // `Model::ownedResources_` is a `shared_ptr<void>`, so a copy shares the buffers rather than
    // duplicating them -- which is the right choice and an easy one to break. The property worth
    // pinning is the one a caller depends on without knowing it: the copy stays valid after the
    // original is destroyed.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);

    Microsoft::Xna::Framework::Graphics::VertexBuffer* originalBuffer = nullptr;
    Model copy = [&] {
        Model original = cm.Load<Model>(kOrdinary);
        originalBuffer = original.getMeshesProperty()[0]->getMeshPartsProperty()[0]
                             ->getVertexBufferProperty();
        Model duplicate = original;  // copy, not move
        EXPECT_EQ(originalBuffer,
                  duplicate.getMeshesProperty()[0]->getMeshPartsProperty()[0]
                      ->getVertexBufferProperty())
            << "a copied Model duplicated its vertex buffer instead of sharing it";
        return duplicate;
    }();

    // The original is gone. The copy must still describe the same buffers -- if `ownedResources_`
    // were not shared, this is where the use-after-free would be.
    ASSERT_EQ(1, copy.getMeshesProperty().getCountProperty());
    auto* part = copy.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    ASSERT_NE(nullptr, part);
    EXPECT_EQ(originalBuffer, part->getVertexBufferProperty())
        << "the surviving copy points at a different buffer than the original did";
    EXPECT_GT(part->getPrimitiveCountProperty(), 0);
}

// --- GLTF-437: what is cached within a load, and what is not across loads --------------------------

TEST(GltfPerformance, TexturesAreSharedWithinAManagerAndNotAcrossManagers)
{
    // GLTF-437, restated by measurement. "Correct within a load; nothing across loads" is nearly
    // right and off by one level: the sharing boundary is the **ContentManager**, not the load.
    // Within one, a second `Load` of the same name returns the cached model and decodes nothing;
    // across two managers the same asset is decoded twice, textures included. That is the cost,
    // and the decision is to keep it -- two managers is exactly how an application asks for two
    // independent copies, and a global texture cache would make `Unload` on one free the other's.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);

    Model first = cm.Load<Model>("tex-reference-checkerboard");
    Model second = cm.Load<Model>("tex-reference-checkerboard");
    auto* firstEffect = first.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty();
    auto* secondEffect = second.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty();
    ASSERT_NE(nullptr, firstEffect);
    EXPECT_EQ(firstEffect, secondEffect)
        << "one manager decoded the same asset twice -- the cache is not hit for a textured model";

    ContentManager other(nullptr, CorpusDirectory().string());
    other.setGraphicsDevice(gd);
    Model elsewhere = other.Load<Model>("tex-reference-checkerboard");
    auto* elsewhereEffect =
        elsewhere.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty();
    ASSERT_NE(nullptr, elsewhereEffect);
    EXPECT_NE(firstEffect, elsewhereEffect)
        << "two managers shared an effect, so there is a process-wide cache and Unload on one "
           "would leave the other pointing at freed textures";
}

// --- GLTF-443: what the occlusion remap costs ------------------------------------------------------

TEST(GltfPerformance, OcclusionRemapCodecCostIsMeasured)
{
    // The dual-texture lightmap approximation decodes the occlusion PNG, halves every pixel and
    // re-encodes it as PNG -- which the texture loader then decodes a second time. Three codec
    // passes for an arithmetic operation on one channel. The row asks for that to be measured and
    // short-circuited where possible; measuring it first is what says whether "where possible" is
    // worth anyone's afternoon.
    using CNA::Internal::GltfImport::ExtractedImage;
    using CNA::Internal::GltfImport::RemapOcclusionImageForDualTextureEXT;

    // A real PNG from the corpus, so the measurement is of the codec rather than of a synthetic
    // buffer that compresses to nothing.
    const LoadedFixture fixture("tex-dual-texture-stride");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const cgltf_data& data = fixture.Data();
    ASSERT_GT(data.images_count, 0u);

    const std::optional<ExtractedImage> image = CNA::Internal::GltfImport::ExtractImage(
        &data.images[0], CorpusDirectory());
    ASSERT_TRUE(image.has_value()) << "the fixture's occlusion image could not be extracted";
    RecordProperty("occlusionImageBytes", static_cast<int>(image->bytes.size()));

    constexpr int kIterations = 200;
    std::size_t remappedBytes = 0;
    const double msPerRemap = MillisecondsPerCall(kIterations, [&] {
        const std::optional<ExtractedImage> remapped = RemapOcclusionImageForDualTextureEXT(*image);
        if (remapped.has_value()) { remappedBytes = remapped->bytes.size(); }
    });
    RecordProperty("msPerOcclusionRemap_x1000", static_cast<int>(msPerRemap * 1000.0));
    RecordProperty("remappedImageBytes", static_cast<int>(remappedBytes));
    EXPECT_GT(remappedBytes, 0u) << "the remap produced nothing";
    EXPECT_LT(msPerRemap, 25.0)
        << "one occlusion remap of a small image took " << msPerRemap << " ms";
}

// --- GLTF-441 / GLTF-442: what a morph weight change costs, in time and in memory -----------------

TEST(GltfPerformance, MorphReuploadCostAndItsMemoryDuplicationAreMeasured)
{
    // Morphing is CPU-side: `SetMorphWeightsEXT` re-blends the whole vertex array from
    // `BaseVertexBytes` and re-uploads it. Two costs follow, and the row asks for both as numbers
    // rather than as an apology.
    //
    // The memory one is exact rather than sampled: a morphed part keeps the rest pose
    // (`BaseVertexBytes`) alongside the buffer it uploads, so its vertex data is stored twice.
    // That is arithmetic, identical on every machine, and it is the number that decides whether a
    // GPU morph path is worth building -- 2x on a 200 k-vertex mesh at stride 48 is 19 MB, not a
    // rounding error.
    using Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT;
    using Microsoft::Xna::Framework::Graphics::SetMorphWeightsEXT;

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("morph-node-weights-override");

    Microsoft::Xna::Framework::Graphics::ModelMeshPart* morphed = nullptr;
    for (int mi = 0; mi < model.getMeshesProperty().getCountProperty() && morphed == nullptr; ++mi)
    {
        auto* mesh = model.getMeshesProperty()[mi];
        for (int pi = 0; pi < mesh->getMeshPartsProperty().getCountProperty(); ++pi)
        {
            auto* part = mesh->getMeshPartsProperty()[pi];
            if (part != nullptr && dynamic_cast<MorphTargetDataEXT*>(part->getTagProperty()) != nullptr)
            {
                morphed = part;
                break;
            }
        }
    }
    ASSERT_NE(nullptr, morphed) << "the morph fixture produced no part carrying MorphTargetDataEXT";

    auto* morphData = dynamic_cast<MorphTargetDataEXT*>(morphed->getTagProperty());
    ASSERT_NE(nullptr, morphData);
    const std::size_t baseBytes = morphData->BaseVertexBytes.size();
    ASSERT_GT(baseBytes, 0u);
    RecordProperty("morphBaseVertexBytes", static_cast<int>(baseBytes));
    RecordProperty("morphTargetCount", static_cast<int>(morphData->PositionDeltas.size()));
    // The duplication factor, stated as what it is: the uploaded buffer plus the rest pose.
    RecordProperty("morphVertexBytesStoredTimes100", 200);

    const std::vector<float> weightsA(morphData->PositionDeltas.size(), 0.25f);
    const std::vector<float> weightsB(morphData->PositionDeltas.size(), 0.75f);
    constexpr int kIterations = 200;
    bool flip = false;
    const double msPerReupload = MillisecondsPerCall(kIterations, [&] {
        SetMorphWeightsEXT(*morphed, flip ? weightsA : weightsB);
        flip = !flip;
    });
    RecordProperty("msPerMorphReupload_x1000", static_cast<int>(msPerReupload * 1000.0));
    EXPECT_LT(msPerReupload, 25.0)
        << "one morph weight change on a tiny mesh took " << msPerReupload
        << " ms -- the blend is no longer linear in vertex count";
}
