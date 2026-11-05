// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
using namespace Microsoft::Xna::Framework::Graphics;
namespace {
template<class F> void DeclarationError(F&& action, const char* message)
{
    try { action(); ADD_FAILURE() << "malformed declaration accepted"; }
    catch (const System::ArgumentException& error) {
        EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
        EXPECT_TRUE(error.getParamNameProperty().empty());
        EXPECT_EQ(error.getMessageProperty(), message);
    }
    catch (...) { ADD_FAILURE() << "wrong exception type"; }
}
}
// Refresh VertexDeclaration.GetVertexElements IL_0000-0010 calls Array.Clone;
// .ctor(int,elements) IL_0011-001f clones the caller input before validation.
TEST(VertexDeclarationRefreshTest, ElementSnapshotsAreIndependentAndSurviveAssignment)
{
    std::vector<VertexElement> source{
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)};
    VertexDeclaration declaration(source);
    source[0].setOffsetProperty(8);
    const auto& first = declaration.GetVertexElements();
    const auto& second = declaration.GetVertexElements();
    EXPECT_NE(&first, &second); // Two array clones, not two aliases of the same vector.
    EXPECT_EQ(first, (std::vector<VertexElement>{
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)}));
    auto writable = declaration.GetVertexElements();
    writable[0].setOffsetProperty(4);
    EXPECT_EQ(declaration.GetVertexElements()[0].getOffsetProperty(), 0);
    declaration = VertexDeclaration(16, {
        VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1)});
    ASSERT_EQ(first.size(), 2U);
    EXPECT_EQ(first[0].getOffsetProperty(), 0);
    EXPECT_EQ(first[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(first[1].getVertexElementUsageProperty(), VertexElementUsage::Color);
    EXPECT_EQ(second, first);
}

// Refresh VertexElementValidator.Validate(int,elements): alignment IL_000f-001e/0117-0127,
// usage IL_0065-00c0, outside stride IL_00c1-0116, duplicate IL_0128-01ab,
// overlap IL_01bf-0238. Exact messages come from the shipped FrameworkResources.
TEST(VertexDeclarationRefreshTest, StructuralDiagnosticsNameTheElementsAndHonorValidationOrder)
{
    DeclarationError([] { VertexDeclaration d(15, {
        VertexElement(-1, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)}); },
        "Invalid VertexDeclaration. Vertex stride and VertexElement.Offset must be multiples of four.");
    DeclarationError([] { VertexDeclaration d(16, {
        VertexElement(-1, VertexElementFormat::Single, static_cast<VertexElementUsage>(13), 2)}); },
        "Invalid VertexDeclaration. Usage 13 is out of range."); // Bad usage precedes bad offset.
    DeclarationError([] { VertexDeclaration d(16, {
        VertexElement(-1, VertexElementFormat::Single, VertexElementUsage::TextureCoordinate, 3)}); },
        "Invalid VertexDeclaration. Element TextureCoordinate3 does not fit within the specified vertex stride.");
    DeclarationError([] { VertexDeclaration d(16, {
        VertexElement(14, VertexElementFormat::Single, VertexElementUsage::Color, 2)}); },
        "Invalid VertexDeclaration. Element Color2 does not fit within the specified vertex stride."); // Bounds precede alignment.
    DeclarationError([] { VertexDeclaration d(16, {
        VertexElement(2, VertexElementFormat::Single, VertexElementUsage::Position, 0)}); },
        "Invalid VertexDeclaration. Vertex stride and VertexElement.Offset must be multiples of four.");
    DeclarationError([] { VertexDeclaration d(16, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 1),
        VertexElement(4, VertexElementFormat::Single, VertexElementUsage::Position, 1)}); },
        "Invalid VertexDeclaration. Duplicate element Position1."); // Duplicate precedes overlap.
    DeclarationError([] { VertexDeclaration d(16, {
        VertexElement(4, VertexElementFormat::Vector2, VertexElementUsage::Normal, 2),
        VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 3)}); },
        "Invalid VertexDeclaration. Elements Normal2 and TextureCoordinate3 are overlapping.");
}

// Refresh both VertexDeclaration constructors reject null/empty with NullNotAllowed;
// explicit .ctor IL_0006-000f/003f-004e, inferred .ctor IL_0006-000f/004b-005a.
TEST(VertexDeclarationRefreshTest, EmptyElementsPrecedeStrideErrorsWithTheRefreshMessage)
{
    const auto verify = [](auto&& action) {
        try { action(); ADD_FAILURE() << "empty declaration accepted"; }
        catch (const System::ArgumentNullException& error) {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentNullException));
            EXPECT_EQ(error.getParamNameProperty(), "elements");
            const auto message = error.getMessageProperty();
            EXPECT_TRUE(message == "This method does not accept null for this parameter.\nParameter name: elements" ||
                        message == "This method does not accept null for this parameter. (Parameter 'elements')") << message;
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    };
    verify([] { VertexDeclaration d(std::initializer_list<VertexElement>{}); });
    verify([] { VertexDeclaration d(std::vector<VertexElement>{}); });
    verify([] { VertexDeclaration d(-1, std::initializer_list<VertexElement>{}); });
    verify([] { VertexDeclaration d(-1, std::vector<VertexElement>{}); });
}
