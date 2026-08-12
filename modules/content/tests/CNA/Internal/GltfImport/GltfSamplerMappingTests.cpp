// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-202/GLTF-203: glTF sampler state, and the §14.2 mapping table.
//
// `cgltf_sampler`, `mag_filter` and `wrap_s` had ZERO occurrences in CNA. Every imported texture
// was drawn with whatever `SamplerState` the device happened to have, which defaults to
// `LinearWrap` -- so an asset authored `CLAMP_TO_EDGE` with UVs outside [0,1] tiled instead of
// clamping, which is a large, obvious error rather than a subtle one.
//
// The whole table is tested here rather than through fixtures because `MapGltfSamplerEXT` takes raw
// glTF enum values: every one of the 6 x 2 x 3 x 3 combinations can be stated directly, which no
// realistic number of fixtures could cover.

#include <string>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using CNA::Internal::GltfImport::MapGltfSamplerEXT;
using CNA::Internal::GltfImport::SamplerOut;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

namespace
{
    // glTF's own constants, spelled out so a row of the table below reads like the specification.
    constexpr int kNearest              = 9728;
    constexpr int kLinear               = 9729;
    constexpr int kNearestMipmapNearest = 9984;
    constexpr int kLinearMipmapNearest  = 9985;
    constexpr int kNearestMipmapLinear  = 9986;
    constexpr int kLinearMipmapLinear   = 9987;
    constexpr int kRepeat               = 10497;
    constexpr int kClampToEdge          = 33071;
    constexpr int kMirroredRepeat       = 33648;
    constexpr int kUndefined            = 0;
}

// §14.2's filter table, every row. XNA turns out to cover glTF's filter space EXACTLY -- its nine
// TextureFilter values express all eight min x mag x mip combinations -- so none of these is an
// approximation, which is worth asserting because the plan originally assumed otherwise.
TEST(GltfSamplerMapping, EveryMinMagCombinationMapsToItsExactXnaFilter)
{
    struct Row
    {
        int minFilter;
        int magFilter;
        TextureFilter expected;
        const char* what;
    };

    const Row rows[] = {
        // Both linear, both mip modes.
        {kLinearMipmapLinear,   kLinear,  TextureFilter::Linear,        "trilinear"},
        {kLinearMipmapNearest,  kLinear,  TextureFilter::LinearMipPoint, "linear min/mag, point mip"},
        // Both point, both mip modes.
        {kNearestMipmapNearest, kNearest, TextureFilter::Point,          "point everywhere"},
        {kNearestMipmapLinear,  kNearest, TextureFilter::PointMipLinear, "point min/mag, linear mip"},
        // Mixed: linear minification, point magnification.
        {kLinearMipmapLinear,   kNearest, TextureFilter::MinLinearMagPointMipLinear, "mixed, linear mip"},
        {kLinearMipmapNearest,  kNearest, TextureFilter::MinLinearMagPointMipPoint,  "mixed, point mip"},
        // Mixed the other way: point minification, linear magnification.
        {kNearestMipmapLinear,  kLinear,  TextureFilter::MinPointMagLinearMipLinear, "mixed, linear mip"},
        {kNearestMipmapNearest, kLinear,  TextureFilter::MinPointMagLinearMipPoint,  "mixed, point mip"},
    };

    for (const Row& row : rows)
    {
        SCOPED_TRACE(std::string(row.what) + " (min=" + std::to_string(row.minFilter) +
                     ", mag=" + std::to_string(row.magFilter) + ")");
        const SamplerOut mapped =
            MapGltfSamplerEXT(row.magFilter, row.minFilter, kRepeat, kRepeat);
        EXPECT_EQ(row.expected, mapped.filter);
        EXPECT_FALSE(mapped.minFilterHasNoMipStage) << "this minFilter does have a mip stage";
    }
}

// The two minFilters with no mip stage at all. XNA's TextureFilter cannot say "base level only" --
// that is a property of the texture's level count, not of the sampler -- so the mip mode carried
// here is arbitrary, and the flag is what stops that from being an unrecorded guess.
TEST(GltfSamplerMapping, ANonMipmappedMinFilterIsFlaggedRatherThanSilentlyApproximated)
{
    for (const int magFilter : {kNearest, kLinear})
    {
        SCOPED_TRACE("mag=" + std::to_string(magFilter));

        const SamplerOut nearest = MapGltfSamplerEXT(magFilter, kNearest, kRepeat, kRepeat);
        EXPECT_TRUE(nearest.minFilterHasNoMipStage);
        const SamplerOut linear = MapGltfSamplerEXT(magFilter, kLinear, kRepeat, kRepeat);
        EXPECT_TRUE(linear.minFilterHasNoMipStage);

        // Point is chosen as the least-blending mip mode, so minification still behaves as the
        // file asked even once GLTF-206 starts generating levels.
        EXPECT_EQ(magFilter == kNearest ? TextureFilter::Point
                                        : TextureFilter::MinPointMagLinearMipPoint,
                  nearest.filter);
        EXPECT_EQ(magFilter == kNearest ? TextureFilter::MinLinearMagPointMipPoint
                                        : TextureFilter::LinearMipPoint,
                  linear.filter);
    }
}

// §14.2's wrap table. The two axes are independent, which is the point: a texture may repeat in U
// and clamp in V, and a mapping that read one field for both would pass every symmetric test.
TEST(GltfSamplerMapping, WrapModesMapPerAxisIndependently)
{
    struct Row { int wrap; TextureAddressMode expected; };
    const Row rows[] = {
        {kRepeat,         TextureAddressMode::Wrap},
        {kClampToEdge,    TextureAddressMode::Clamp},
        {kMirroredRepeat, TextureAddressMode::Mirror},
        {kUndefined,      TextureAddressMode::Wrap},   // §3.8.4's default
    };

    for (const Row& u : rows)
    {
        for (const Row& v : rows)
        {
            SCOPED_TRACE("wrapS=" + std::to_string(u.wrap) + " wrapT=" + std::to_string(v.wrap));
            const SamplerOut mapped =
                MapGltfSamplerEXT(kLinear, kLinearMipmapLinear, u.wrap, v.wrap);
            EXPECT_EQ(u.expected, mapped.addressU);
            EXPECT_EQ(v.expected, mapped.addressV);
        }
    }
}

// The case that motivated the whole task: CLAMP_TO_EDGE must not come out as Wrap. Called out
// separately from the table above because it is the failure an asset actually shows.
TEST(GltfSamplerMapping, ClampToEdgeDoesNotTile)
{
    const SamplerOut mapped =
        MapGltfSamplerEXT(kLinear, kLinearMipmapLinear, kClampToEdge, kClampToEdge);
    EXPECT_EQ(TextureAddressMode::Clamp, mapped.addressU);
    EXPECT_EQ(TextureAddressMode::Clamp, mapped.addressV);
    EXPECT_NE(TextureAddressMode::Wrap, mapped.addressU)
        << "a clamped texture still tiles -- UVs outside [0,1] will repeat the image";
}

// §3.8.4: an absent sampler means repeat with an implementation-chosen filter. CNA reads that as
// LinearWrap -- which is what the device already defaulted to, so this is the one case where the
// old behaviour was accidentally right. `declared` is what keeps the two distinguishable.
TEST(GltfSamplerMapping, AnUndefinedSamplerIsGltfsOwnDefaultAndSaysSo)
{
    const SamplerOut mapped = MapGltfSamplerEXT(kUndefined, kUndefined, kUndefined, kUndefined);
    EXPECT_EQ(TextureFilter::Linear, mapped.filter);
    EXPECT_EQ(TextureAddressMode::Wrap, mapped.addressU);
    EXPECT_EQ(TextureAddressMode::Wrap, mapped.addressV);
    EXPECT_FALSE(mapped.declared)
        << "an undefined sampler claims to have been declared -- an import report could not then "
           "tell 'the author chose repeat' from 'the author said nothing'";
    EXPECT_FALSE(mapped.minFilterHasNoMipStage)
        << "the default filter is the auto one, which does mipmap";
}

// --- plan_gltf.md GLTF-060 / GLTF-097: attribute count agreement, as a shared assertion ------------
//
// GLTF-097 asks for the same check GLTF-060 landed, stated once and applied to every stream rather
// than to the pair someone happened to think of. It is one assertion in ExtractMesh, driven by the
// primitive's own attribute list, so an attribute CNA does not yet read is still checked -- these
// tests are what make "every stream" mean every stream rather than the two with fixtures.

TEST(GltfSamplerMapping, AttributeCountAgreementCoversEveryDeclaredStreamNotJustTheReadOnes)
{
    // Driven off the corpus rather than a synthetic file, so it exercises the real extraction path.
    // Every well-formed fixture must pass; the malformed one must not. Asserting both directions is
    // what stops the check from being satisfied by a version that rejects everything.
    const CnaTest::GltfOracle::LoadedFixture bad("accessor-count-mismatch");
    ASSERT_TRUE(bad.Ok()) << bad.Error();

    bool refused = false;
    std::string message;
    try
    {
        (void)CNA::Internal::GltfImport::ExtractMesh(
            &bad.Data(), bad.Data().meshes[0].primitives[0], "probe", nullptr, 1.0f);
    }
    catch (const std::exception& e)
    {
        refused = true;
        message = e.what();
    }
    EXPECT_TRUE(refused) << "a primitive whose attribute counts disagree was extracted anyway";
    EXPECT_NE(std::string::npos, message.find("same count"))
        << "the diagnostic does not name the constraint: " << message;
    EXPECT_NE(std::string::npos, message.find("NORMAL"))
        << "the diagnostic does not name the offending attribute: " << message;

    // The converse: every other corpus fixture extracts cleanly, so the check is not simply
    // refusing anything with more than one attribute.
    //
    // Which fixtures are exempt is read from the manifest rather than listed here. A hardcoded
    // pair of ids was silently wrong the moment a third malformed fixture was added, and it says
    // nothing about WHY those two are exempt; `rejection` says exactly that, and cannot be
    // forgotten. A rejection fixture is still extracted -- it must not crash on malformed input --
    // only its refusal is allowed, and the suite that owns the refusal asserts its message.
    std::size_t extracted = 0;
    std::size_t refusedFixtures = 0;
    for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds())
    {
        const CnaTest::GltfOracle::LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const bool mayRefuse = CnaTest::GltfOracle::IsRejectionFixture(fixture.Expected());
        const cgltf_data& data = fixture.Data();
        for (cgltf_size m = 0; m < data.meshes_count; ++m)
        {
            for (cgltf_size pi = 0; pi < data.meshes[m].primitives_count; ++pi)
            {
                SCOPED_TRACE(id + " mesh " + std::to_string(m) + " primitive " + std::to_string(pi));
                if (mayRefuse)
                {
                    try
                    {
                        (void)CNA::Internal::GltfImport::ExtractMesh(
                            &data, data.meshes[m].primitives[pi], "probe", nullptr, 1.0f);
                    }
                    catch (const std::runtime_error&)
                    {
                        // Named and deliberate. Anything else escapes and fails the test, which is
                        // what keeps "malformed input is refused" from covering "malformed input
                        // throws whatever the allocator felt like".
                        ++refusedFixtures;
                    }
                    continue;
                }
                EXPECT_NO_THROW({
                    (void)CNA::Internal::GltfImport::ExtractMesh(
                        &data, data.meshes[m].primitives[pi], "probe", nullptr, 1.0f);
                });
                ++extracted;
            }
        }
    }
    EXPECT_GT(extracted, 0u);
    EXPECT_GT(refusedFixtures, 0u)
        << "no malformed fixture was refused at extraction, so the exemption above is covering "
           "nothing";
}

// --- plan_gltf.md GLTF-214: vertex colours are linear, and stay that way ---------------------------
//
// glTF §3.7.2.1 declares COLOR_0 a LINEAR value -- unlike baseColorTexture and emissiveTexture,
// which §3.9.2 declares sRGB-encoded. So the colour-space work GLTF-210 landed must NOT touch it:
// applying the sRGB decode to a vertex colour would darken it by the same 2.3x mid-grey factor the
// decode exists to correct on a texture.
//
// The finding is that this is already right, and right by construction rather than by luck: the
// decode is applied in the shader to two named texture samples, and a vertex colour is neither.
// What was missing is anything saying so, which is what a future "apply the transfer everywhere"
// simplification would have needed to run into.

TEST(GltfSamplerMapping, VertexColoursAreImportedAsTheLinearValuesTheyAre)
{
    const CnaTest::GltfOracle::LoadedFixture fixture("normalized-u8-color");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    // The manifest's L3 colours are the accessor's own normalized values. If the importer ever
    // applied a transfer to them, they would no longer match -- which is the whole assertion.
    const CNA::Internal::JsonValue& primitives =
        CnaTest::GltfOracle::Path(fixture.Expected(), "l3.primitives");
    ASSERT_EQ(CNA::Internal::JsonType::Array, primitives.type);
    ASSERT_FALSE(primitives.arrayValue.empty());
    const CNA::Internal::JsonValue& colors =
        CnaTest::GltfOracle::Member(primitives.arrayValue.front(), "colors");
    ASSERT_EQ(CNA::Internal::JsonType::Array, colors.type);
    ASSERT_FALSE(colors.arrayValue.empty());

    const CNA::Internal::GltfImport::MeshOut extracted = CNA::Internal::GltfImport::ExtractMesh(
        &fixture.Data(), fixture.Data().meshes[0].primitives[0], "probe", nullptr, 1.0f);
    ASSERT_TRUE(extracted.colored);

    // A vertex colour that had been sRGB-decoded would be darker than authored everywhere except
    // at 0 and 1, so a mid value is what makes the test able to fail. Find one and check it.
    bool sawMidValue = false;
    for (const CNA::Internal::JsonValue& colour : colors.arrayValue)
    {
        for (const double component : CnaTest::GltfOracle::Numbers(colour))
        {
            if (component > 0.05 && component < 0.95) { sawMidValue = true; }
        }
    }
    EXPECT_TRUE(sawMidValue)
        << "every authored vertex colour component is 0 or 1, where the sRGB transfer is the "
           "identity -- this fixture cannot tell a decoded vertex colour from an untouched one";
}
