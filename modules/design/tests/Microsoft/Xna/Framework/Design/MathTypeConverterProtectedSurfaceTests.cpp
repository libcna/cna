// SPDX-License-Identifier: MS-PL
//
// The documented protected surface of MathTypeConverter and the three ConvertFrom overrides that
// BoundingBoxConverter, BoundingSphereConverter and RayConverter declare. XNA's own bodies for
// those three are `return base.ConvertFrom(context, culture, value);`
// (xna4-decomp/.../Microsoft.Xna.Framework.Design/*.cs), so what is worth testing is that the
// override is reachable and forwards, and that the two protected fields are the documented names
// a subclass can see and set.

#include <gtest/gtest.h>

#include <any>
#include <string>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Design/BoundingBoxConverter.hpp"
#include "Microsoft/Xna/Framework/Design/BoundingSphereConverter.hpp"
#include "Microsoft/Xna/Framework/Design/MathTypeConverter.hpp"
#include "Microsoft/Xna/Framework/Design/RayConverter.hpp"
#include "Microsoft/Xna/Framework/Design/Vector3Converter.hpp"
#include "System/ComponentModel/Design/Serialization/InstanceDescriptor.hpp"
#include "System/ComponentModel/PropertyDescriptorCollection.hpp"
#include "System/Globalization/CultureInfo.hpp"
#include "System/NotSupportedException.hpp"
#include "System/Type.hpp"

using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Vector3;
using System::ComponentModel::Design::Serialization::InstanceDescriptor;
using namespace Microsoft::Xna::Framework::Design;

namespace
{
    /// A subclass reaching the two documented protected fields by their XNA names. If either field
    /// were renamed or duplicated behind an alias, this would not compile.
    class ProbeConverter final : public MathTypeConverter
    {
    public:
        ProbeConverter() { supportStringConvert = false; }

        [[nodiscard]] bool SeesSupportStringConvert() const { return supportStringConvert; }

        void EnableStringConvert() { supportStringConvert = true; }

        [[nodiscard]] std::size_t DescriptorCount() const { return propertyDescriptions.getCountProperty(); }

        void AdoptDescriptors(const System::ComponentModel::PropertyDescriptorCollection& value)
        {
            propertyDescriptions = value;
        }
    };
}

TEST(MathTypeConverterProtectedSurfaceTest, ASubclassReadsAndWritesTheDocumentedFields)
{
    ProbeConverter converter;

    // The constructor's own write is visible, which is how the XNA converters configure themselves.
    EXPECT_FALSE(converter.SeesSupportStringConvert());
    converter.EnableStringConvert();
    EXPECT_TRUE(converter.SeesSupportStringConvert());
}

TEST(MathTypeConverterProtectedSurfaceTest, TheFieldsAreOneStateSourceNotAnAliasPair)
{
    // supportStringConvert is the flag CanConvertFrom consults, so a subclass write has to change
    // the converter's observable answer. Two synchronized copies would let these diverge.
    ProbeConverter converter;
    const System::Type& stringType = System::Type::From<std::string>();
    EXPECT_FALSE(converter.CanConvertFrom(nullptr, stringType));
    converter.EnableStringConvert();
    EXPECT_TRUE(converter.CanConvertFrom(nullptr, stringType));
}

TEST(MathTypeConverterProtectedSurfaceTest, ASubclassWriteToPropertyDescriptionsIsWhatGetPropertiesReturns)
{
    ProbeConverter converter;
    ASSERT_EQ(converter.DescriptorCount(), 0u);

    // Borrow a populated collection from a converter that fills one in its constructor.
    const BoundingBoxConverter populated;
    const auto descriptors = populated.GetProperties(
        nullptr, std::any(BoundingBox(Vector3(0.0f), Vector3(1.0f))), {});
    ASSERT_GT(descriptors.getCountProperty(), 0u);

    converter.AdoptDescriptors(descriptors);
    EXPECT_EQ(converter.DescriptorCount(), descriptors.getCountProperty());
    EXPECT_EQ(converter.GetProperties(nullptr, std::any(BoundingBox()), {}).getCountProperty(),
              descriptors.getCountProperty());
}

namespace
{
    /// XNA's three overrides are `return base.ConvertFrom(...)`, and the base converter invokes an
    /// InstanceDescriptor and refuses everything else. Both halves of that are asserted here, so
    /// the override is shown to forward rather than to intercept.
    template <typename TConverter, typename TValue>
    void ExpectConvertFromForwardsToTheBase(const TValue& value)
    {
        const TConverter converter;
        const System::Globalization::CultureInfo& culture =
            System::Globalization::CultureInfo::getInvariantCultureProperty();

        // The forwarding path that succeeds: the base invokes the descriptor's constructor.
        const std::any described =
            converter.ConvertTo(nullptr, &culture, std::any(value), System::Type::From<InstanceDescriptor>());
        ASSERT_NE(std::any_cast<InstanceDescriptor>(&described), nullptr);
        const std::any roundTripped = converter.ConvertFrom(nullptr, &culture, described);
        const TValue* result = std::any_cast<TValue>(&roundTripped);
        ASSERT_NE(result, nullptr);
        EXPECT_TRUE(result->Equals(value));

        // The forwarding path that refuses: anything else reaches the base's convert-from failure.
        EXPECT_THROW((void)converter.ConvertFrom(nullptr, &culture, std::any(value)),
                     System::NotSupportedException);
        EXPECT_THROW((void)converter.ConvertFrom(nullptr, &culture, std::any(7)),
                     System::NotSupportedException);
        EXPECT_THROW((void)converter.ConvertFrom(nullptr, &culture, std::any()),
                     System::NotSupportedException);
    }
}

TEST(MathTypeConverterProtectedSurfaceTest, BoundingBoxConverterConvertFromForwardsToTheBase)
{
    ExpectConvertFromForwardsToTheBase<BoundingBoxConverter>(
        BoundingBox(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f)));
}

TEST(MathTypeConverterProtectedSurfaceTest, BoundingSphereConverterConvertFromForwardsToTheBase)
{
    ExpectConvertFromForwardsToTheBase<BoundingSphereConverter>(
        BoundingSphere(Vector3(1.0f, 2.0f, 3.0f), 4.0f));
}

TEST(MathTypeConverterProtectedSurfaceTest, RayConverterConvertFromForwardsToTheBase)
{
    ExpectConvertFromForwardsToTheBase<RayConverter>(
        Ray(Vector3(0.0f), Vector3(0.0f, 0.0f, 1.0f)));
}

TEST(MathTypeConverterProtectedSurfaceTest, TheThreeConvertersStillRefuseAStringSource)
{
    // Their constructors clear supportStringConvert, and the forwarding ConvertFrom must not have
    // introduced a string route that XNA does not have.
    const System::Type& stringType = System::Type::From<std::string>();
    EXPECT_FALSE(BoundingBoxConverter().CanConvertFrom(nullptr, stringType));
    EXPECT_FALSE(BoundingSphereConverter().CanConvertFrom(nullptr, stringType));
    EXPECT_FALSE(RayConverter().CanConvertFrom(nullptr, stringType));

    // A converter that leaves the flag set still does, so the assertion above is about these three.
    EXPECT_TRUE(Vector3Converter().CanConvertFrom(nullptr, stringType));
}

TEST(MathTypeConverterProtectedSurfaceTest, ConvertFromIsReachedThroughTheBaseConverterInterface)
{
    // A CLR caller reaches ConvertFrom through TypeConverter; the override must dispatch there.
    const BoundingBoxConverter converter;
    const System::ComponentModel::TypeConverter& asBase = converter;
    const BoundingBox value(Vector3(0.0f), Vector3(2.0f));
    const System::Globalization::CultureInfo& culture =
        System::Globalization::CultureInfo::getInvariantCultureProperty();

    const std::any described =
        asBase.ConvertTo(nullptr, &culture, std::any(value), System::Type::From<InstanceDescriptor>());
    const std::any roundTripped = asBase.ConvertFrom(nullptr, &culture, described);
    const BoundingBox* result = std::any_cast<BoundingBox>(&roundTripped);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->Equals(value));

    EXPECT_THROW((void)asBase.ConvertFrom(nullptr, &culture, std::any(value)),
                 System::NotSupportedException);
}
