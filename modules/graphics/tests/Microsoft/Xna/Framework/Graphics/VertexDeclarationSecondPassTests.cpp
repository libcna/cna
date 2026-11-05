// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ArgumentException.hpp"
#include "System/EventArgs.hpp"
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
template<class F> void ExpectStrideError(F&& construct)
{
    try { construct(); ADD_FAILURE() << "nonpositive stride accepted"; }
    catch (const System::ArgumentOutOfRangeException& error) {
        EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
        EXPECT_EQ(error.getParamNameProperty(), "vertexStride");
        // Refresh validator IL_0004-000e calls the BCL parameter-only constructor.
        // SharpRuntime supports both Framework and modern parameter suffix styles.
        const auto message = error.getMessageProperty();
        EXPECT_TRUE(message == "Specified argument was out of the range of valid values.\nParameter name: vertexStride" ||
                    message == "Specified argument was out of the range of valid values. (Parameter 'vertexStride')") << message;
    }
    catch (...) { ADD_FAILURE() << "wrong exception type"; }
}
class DeclarationTag final : public System::Object {
public:
    const std::string& GetTypeName() const override {
        static const std::string name = "DeclarationTag";
        return name;
    }
};
}

TEST(VertexDeclarationSecondPassTest, NonpositiveStridePrecedesEveryElementError)
{
    const std::vector<VertexElement> malformed{
        VertexElement(-1, VertexElementFormat::Vector3, static_cast<VertexElementUsage>(13), 16)};
    for (const int stride : {0, -1, -4}) {
        ExpectStrideError([&] { VertexDeclaration declaration(stride, malformed); });
        ExpectStrideError([&] { VertexDeclaration declaration(stride, {malformed[0]}); });
    }
    // max(0, -4 + sizeof(Single)) = 0; inferred stride fails before the negative offset.
    ExpectStrideError([] { VertexDeclaration d({VertexElement(-4, VertexElementFormat::Single, VertexElementUsage::Position, 0)}); });
    ExpectStrideError([] { VertexDeclaration d(std::vector<VertexElement>{VertexElement(-4, VertexElementFormat::Single, VertexElementUsage::Position, 0)}); });
}

// Refresh GetTypeSize IL_0003-0053 yields these twelve literal byte sizes.
// GetVertexStride IL_0006-002a takes max(end), explicit .ctor IL_0024-002d keeps the supplied stride.
TEST(VertexDeclarationSecondPassTest, EveryFormatHasItsLiteralSizeAcrossAllConstructorRoutes)
{
    constexpr int sizes[]{4, 8, 12, 16, 4, 4, 4, 8, 4, 8, 4, 8};
    for (int ordinal = 0; ordinal < 12; ++ordinal) {
        SCOPED_TRACE(ordinal);
        const VertexElement element(8, static_cast<VertexElementFormat>(ordinal), VertexElementUsage::TextureCoordinate, 3);
        const std::vector<VertexElement> input{element};
        const VertexDeclaration inferredVector(input);
        const VertexDeclaration inferredList({element});
        const VertexDeclaration explicitVector(32, input);
        const VertexDeclaration explicitList(32, {element});
        EXPECT_EQ(inferredVector.getVertexStrideProperty(), 8 + sizes[ordinal]);
        EXPECT_EQ(inferredList.getVertexStrideProperty(), 8 + sizes[ordinal]);
        EXPECT_EQ(explicitVector.getVertexStrideProperty(), 32);
        EXPECT_EQ(explicitList.getVertexStrideProperty(), 32);
        EXPECT_EQ(inferredVector.GetVertexElements(), input);
        EXPECT_EQ(inferredList.GetVertexElements(), input);
        EXPECT_EQ(explicitVector.GetVertexElements(), input);
        EXPECT_EQ(explicitList.GetVertexElements(), input);
    }
}

// Refresh GetTypeSize IL_0054-0055 returns zero for an unknown format;
// structural validation has no format-range check (profile validation IL_004e-0091 does).
TEST(VertexDeclarationSecondPassTest, UnknownFormatsHaveZeroExtentUntilProfileBinding)
{
    for (const int ordinal : {-1, 12}) {
        const VertexElement unknown(16, static_cast<VertexElementFormat>(ordinal), VertexElementUsage::Color, 0);
        const VertexElement real(0, VertexElementFormat::Vector4, VertexElementUsage::Position, 0);
        const std::vector<VertexElement> input{unknown, real};
        const VertexDeclaration inferred(input);
        const VertexDeclaration explicitStride(16, input);
        EXPECT_EQ(inferred.getVertexStrideProperty(), 16); // max(16+0, 0+16).
        EXPECT_EQ(explicitStride.getVertexStrideProperty(), 16);
        EXPECT_EQ(inferred.GetVertexElements(), input);
        EXPECT_EQ(explicitStride.GetVertexElements(), input);
    }
}

// Refresh declaration constructors clone input IL_0011-001f, GetVertexElements clones
// IL_0000-0010. TQ-056 covers inferred-vector snapshots; this covers explicit-vector input.
TEST(VertexDeclarationSecondPassTest, ExplicitStrideCopiesTheCallerArrayAndEveryElementField)
{
    std::vector<VertexElement> input{
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 1),
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)};
    const std::vector<VertexElement> expected = input;
    const VertexDeclaration declaration(24, input);
    input[0] = VertexElement(0, VertexElementFormat::Single, VertexElementUsage::TextureCoordinate, 15);
    input.clear();
    auto snapshot = declaration.GetVertexElements();
    EXPECT_EQ(snapshot, expected);
    snapshot[0].setVertexElementFormatProperty(VertexElementFormat::Single);
    snapshot[0].setVertexElementUsageProperty(VertexElementUsage::Normal);
    snapshot[0].setUsageIndexProperty(9);
    EXPECT_EQ(declaration.GetVertexElements(), expected);
    EXPECT_EQ(declaration.getVertexStrideProperty(), 24);
}

// Refresh GraphicsResource local Name/Tag getters IL_001e-0024, setters IL_0020-0027;
// ~GraphicsResource IL_0000-0026 is idempotent and sets IsDisposed before Disposing.
// VertexDeclaration getters contain no disposal guard (stride IL_0000-0006, elements IL_0000-0010).
TEST(VertexDeclarationSecondPassTest, StandaloneDisposalPreservesMetadataAndSnapshotsAndRaisesOnce)
{
    DeclarationTag tag;
    VertexDeclaration declaration(12, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    EXPECT_TRUE(declaration.getNameProperty().empty()); // C++ maps null Name to empty string.
    EXPECT_EQ(declaration.getTagProperty(), nullptr);
    EXPECT_EQ(declaration.getGraphicsDeviceProperty(), nullptr);
    EXPECT_FALSE(declaration.getIsDisposedProperty());
    declaration.setNameProperty("standalone");
    declaration.setTagProperty(&tag);
    const auto snapshot = declaration.GetVertexElements();
    int raised = 0;
    declaration.Disposing += [&](System::Object* sender, const System::EventArgs&) {
        ++raised;
        EXPECT_EQ(sender, &declaration);
        EXPECT_TRUE(declaration.getIsDisposedProperty());
        EXPECT_EQ(declaration.getNameProperty(), "standalone");
        EXPECT_EQ(declaration.getTagProperty(), &tag);
    };
    declaration.Dispose();
    declaration.Dispose();
    EXPECT_EQ(raised, 1);
    EXPECT_TRUE(declaration.getIsDisposedProperty());
    EXPECT_EQ(declaration.getVertexStrideProperty(), 12);
    EXPECT_EQ(declaration.GetVertexElements(), snapshot);
    EXPECT_EQ(declaration.getNameProperty(), "standalone");
    EXPECT_EQ(declaration.getTagProperty(), &tag);
    EXPECT_EQ(declaration.ToString(), "standalone");
    declaration.setNameProperty("after-dispose");
    declaration.setTagProperty(nullptr);
    EXPECT_EQ(declaration.getNameProperty(), "after-dispose");
    EXPECT_EQ(declaration.getTagProperty(), nullptr);
}

// Refresh both .ctor overloads delegate to the same validator. Duplicate IL_0128-01ab
// wins over overlap IL_01bf-0238. The overlap message lists previous then current.
TEST(VertexDeclarationSecondPassTest, EveryConstructorRoutePreservesStructuralDiagnostics)
{
    const auto verify = [](auto&& action, const char* expected) {
        try { action(); ADD_FAILURE() << "malformed layout accepted"; }
        catch (const System::ArgumentException& error) {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_TRUE(error.getParamNameProperty().empty());
            EXPECT_EQ(error.getMessageProperty(), expected);
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    };
    const auto allRoutes = [&](const std::vector<VertexElement>& elements, const char* expected) {
        verify([&] { VertexDeclaration d(elements); }, expected);
        verify([&] { VertexDeclaration d({elements[0], elements[1]}); }, expected);
        verify([&] { VertexDeclaration d(16, elements); }, expected);
        verify([&] { VertexDeclaration d(16, {elements[0], elements[1]}); }, expected);
    };
    allRoutes({VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 1),
               VertexElement(4, VertexElementFormat::Single, VertexElementUsage::Position, 1)},
        "Invalid VertexDeclaration. Duplicate element Position1.");
    allRoutes({VertexElement(4, VertexElementFormat::Vector2, VertexElementUsage::Normal, 2),
               VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 3)},
        "Invalid VertexDeclaration. Elements Normal2 and TextureCoordinate3 are overlapping.");
}
