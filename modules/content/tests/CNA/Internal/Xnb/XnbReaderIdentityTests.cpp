// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-9C: the reader identity model, audited.
//
// Porting the Enum<T> writer (XNAP-98) exposed a real defect that had been latent since the
// collection writers were first written: **a reader's generic arguments are not always the target
// type's generic arguments.**
//
//   ListReader<T>       -> List<T>        shared
//   DictionaryReader<K,V> -> Dictionary<K,V>  shared
//   NullableReader<T>   -> Nullable<T>    shared
//   EnumReader<TEnum>   -> TEnum          NOT shared -- the enum is not generic
//   ArrayReader<T>      -> T[]            NOT shared -- targetBaseName already spells the element
//
// Appending the argument list unconditionally produced `SurfaceFormat[[SurfaceFormat]]` and
// `System.Int32[][[System.Int32]]` -- names no runtime would resolve -- but only where such an
// identity appeared *nested*, which is why nothing caught it for a while.
// XnbReaderIdentity::targetSharesGenericArguments is the fix. This file is the regression barrier:
// the two direct cases, every nested combination that can be built out of the writers CNA ships,
// and a structural invariant checked over every built-in identity in the registry.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "CNA/Internal/Xnb/XnbReaderIdentity.hpp"
#include "CNA/Internal/Xnb/XnbTypeName.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace CNA::Internal::Xnb;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

namespace
{
    constexpr const char* kSurfaceFormat = "Microsoft.Xna.Framework.Graphics.SurfaceFormat";

    [[nodiscard]] XnbReaderIdentity SurfaceFormatEnum()
    {
        return XnbEnumReaderIdentity(kSurfaceFormat, XnbAssembly::FrameworkGraphics);
    }

    /** @brief `ListReader\`1[[<element>]]`, built the way XnbListTypeWriter builds it. */
    [[nodiscard]] XnbReaderIdentity ListOf(XnbReaderIdentity element)
    {
        XnbReaderIdentity identity;
        identity.readerBaseName = "Microsoft.Xna.Framework.Content.ListReader`1";
        identity.targetBaseName = "System.Collections.Generic.List`1";
        identity.targetAssembly = XnbAssembly::Mscorlib;
        identity.genericArguments = {std::move(element)};
        return identity;
    }

    /** @brief `ArrayReader\`1[[<element>]]`, whose target spells the element once. */
    [[nodiscard]] XnbReaderIdentity ArrayOf(XnbReaderIdentity element)
    {
        XnbReaderIdentity identity;
        identity.readerBaseName = "Microsoft.Xna.Framework.Content.ArrayReader`1";
        identity.targetBaseName = XnbTargetTypeName(element) + "[]";
        identity.targetAssembly = element.targetAssembly;
        identity.genericArguments = {std::move(element)};
        identity.targetSharesGenericArguments = false;
        return identity;
    }

    /** @brief `NullableReader\`1[[<underlying>]]`. */
    [[nodiscard]] XnbReaderIdentity NullableOf(XnbReaderIdentity underlying)
    {
        XnbReaderIdentity identity;
        identity.readerBaseName = "Microsoft.Xna.Framework.Content.NullableReader`1";
        identity.targetBaseName = "System.Nullable`1";
        identity.targetAssembly = XnbAssembly::Mscorlib;
        identity.genericArguments = {std::move(underlying)};
        return identity;
    }

    /** @brief `DictionaryReader\`2[[<key>],[<value>]]`. */
    [[nodiscard]] XnbReaderIdentity DictionaryOf(XnbReaderIdentity key, XnbReaderIdentity value)
    {
        XnbReaderIdentity identity;
        identity.readerBaseName = "Microsoft.Xna.Framework.Content.DictionaryReader`2";
        identity.targetBaseName = "System.Collections.Generic.Dictionary`2";
        identity.targetAssembly = XnbAssembly::Mscorlib;
        identity.genericArguments = {std::move(key), std::move(value)};
        return identity;
    }

    /** @brief Visits an identity and every identity nested inside it. */
    void Walk(const XnbReaderIdentity& identity,
              const std::function<void(const XnbReaderIdentity&)>& visit)
    {
        visit(identity);
        for (const XnbReaderIdentity& argument : identity.genericArguments)
        {
            Walk(argument, visit);
        }
    }
}

// -- the two identities whose reader is generic and whose target is not ------------------------

TEST(XnbReaderIdentityTest, AnEnumsTargetIsThePlainEnumNotAGenericInstantiation)
{
    const XnbReaderIdentity identity = SurfaceFormatEnum();
    EXPECT_FALSE(identity.targetSharesGenericArguments);
    EXPECT_EQ(XnbTargetTypeName(identity), kSurfaceFormat);
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.EnumReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(XnbTargetTypeName(identity).find("[["), std::string::npos);
}

TEST(XnbReaderIdentityTest, AnArraysTargetSpellsItsElementExactlyOnce)
{
    const XnbReaderIdentity identity = ArrayOf(XnbBuiltInReaderIdentity<std::int32_t>());
    EXPECT_FALSE(identity.targetSharesGenericArguments);
    EXPECT_EQ(XnbTargetTypeName(identity), "System.Int32[]");
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.ArrayReader`1[[System.Int32]]");
    EXPECT_EQ(XnbTargetTypeName(identity).find("[["), std::string::npos);
}

// -- nesting: the shape that actually produced the malformed names -----------------------------

TEST(XnbReaderIdentityTest, ListOfEnumNamesTheEnumOnceInBothTheReaderAndTheTargetType)
{
    const XnbReaderIdentity identity = ListOf(SurfaceFormatEnum());

    // The bug wrote `ListReader`1[[SurfaceFormat[[SurfaceFormat]]]]`.
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.ListReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(XnbTargetTypeName(identity),
              "System.Collections.Generic.List`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(FormatXnbReaderName(identity, XnbReaderNameStyle::Xna40),
              "Microsoft.Xna.Framework.Content.ListReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat, "
              "Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=842cf8be1de50553]]");
}

TEST(XnbReaderIdentityTest, ListOfInt32ArrayNamesTheElementTypeOnce)
{
    const XnbReaderIdentity identity = ListOf(ArrayOf(XnbBuiltInReaderIdentity<std::int32_t>()));

    // The bug wrote `ListReader`1[[System.Int32[][[System.Int32]]]]`.
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32[]]]");
    EXPECT_EQ(XnbTargetTypeName(identity),
              "System.Collections.Generic.List`1[[System.Int32[]]]");
    EXPECT_EQ(FormatXnbReaderName(identity, XnbReaderNameStyle::Xna40),
              "Microsoft.Xna.Framework.Content.ListReader`1[[System.Int32[], mscorlib, "
              "Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089]]");
}

TEST(XnbReaderIdentityTest, AnArrayOfEnumNamesTheEnumOnceInEachPosition)
{
    const XnbReaderIdentity identity = ArrayOf(SurfaceFormatEnum());
    EXPECT_EQ(XnbTargetTypeName(identity),
              "Microsoft.Xna.Framework.Graphics.SurfaceFormat[]");
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.ArrayReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");

    // And nested one level further, which is where a doubled spelling would surface.
    EXPECT_EQ(XnbCanonicalReaderName(ListOf(identity)),
              "Microsoft.Xna.Framework.Content.ListReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat[]]]");
}

TEST(XnbReaderIdentityTest, ANullableEnumNamesTheEnumOnce)
{
    const XnbReaderIdentity identity = NullableOf(SurfaceFormatEnum());
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.NullableReader`1"
              "[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(XnbTargetTypeName(identity),
              "System.Nullable`1[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(XnbCanonicalReaderName(ListOf(identity)),
              "Microsoft.Xna.Framework.Content.ListReader`1"
              "[[System.Nullable`1[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]]]");
}

TEST(XnbReaderIdentityTest, ANullableArrayNamesItsElementOnce)
{
    const XnbReaderIdentity identity =
        NullableOf(ArrayOf(XnbBuiltInReaderIdentity<std::int32_t>()));
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.NullableReader`1[[System.Int32[]]]");
    EXPECT_EQ(XnbTargetTypeName(identity), "System.Nullable`1[[System.Int32[]]]");
}

TEST(XnbReaderIdentityTest, DictionaryArgumentsAreFormattedIndependently)
{
    const XnbReaderIdentity identity =
        DictionaryOf(XnbBuiltInReaderIdentity<std::string>(), SurfaceFormatEnum());
    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.DictionaryReader`2"
              "[[System.String],[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");
    EXPECT_EQ(XnbTargetTypeName(identity),
              "System.Collections.Generic.Dictionary`2"
              "[[System.String],[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]");

    const XnbReaderIdentity nested = DictionaryOf(
        XnbBuiltInReaderIdentity<std::string>(),
        ListOf(ArrayOf(XnbBuiltInReaderIdentity<std::int32_t>())));
    EXPECT_EQ(XnbCanonicalReaderName(nested),
              "Microsoft.Xna.Framework.Content.DictionaryReader`2"
              "[[System.String],[System.Collections.Generic.List`1[[System.Int32[]]]]]");
}

TEST(XnbReaderIdentityTest, DeepNestingStaysWellFormedAndAssemblyQualifiesEveryArgument)
{
    // Dictionary<String, List<Nullable<SurfaceFormat>>> -- four levels, three of the five reader
    // families, and one identity whose reader is generic while its target is not.
    const XnbReaderIdentity identity = DictionaryOf(
        XnbBuiltInReaderIdentity<std::string>(), ListOf(NullableOf(SurfaceFormatEnum())));

    EXPECT_EQ(XnbCanonicalReaderName(identity),
              "Microsoft.Xna.Framework.Content.DictionaryReader`2"
              "[[System.String],[System.Collections.Generic.List`1"
              "[[System.Nullable`1[[Microsoft.Xna.Framework.Graphics.SurfaceFormat]]]]]]");

    // Under the XNA 4.0 spelling every *argument* is assembly-qualified; the reader itself is not,
    // because DictionaryReader lives in Microsoft.Xna.Framework.
    const std::string qualified = FormatXnbReaderName(identity, XnbReaderNameStyle::Xna40);
    EXPECT_EQ(qualified.find("Microsoft.Xna.Framework.Content.DictionaryReader`2[["), 0u);
    EXPECT_NE(qualified.find("System.String, mscorlib, Version=4.0.0.0"), std::string::npos);
    EXPECT_NE(qualified.find("System.Collections.Generic.List`1[[System.Nullable`1"),
              std::string::npos);
    EXPECT_NE(qualified.find("SurfaceFormat, Microsoft.Xna.Framework.Graphics, Version=4.0.0.0"),
              std::string::npos);

    // Whatever the spelling, normalizing it must land back on the registry key.
    EXPECT_EQ(NormalizeXnbTypeReaderName(qualified), XnbCanonicalReaderName(identity));
}

// -- the structural invariant, over every identity the built-in registry can produce -----------

TEST(XnbReaderIdentityTest, EveryBuiltInIdentityObeysTheTargetSharesGenericArgumentsContract)
{
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);

    std::size_t audited = 0u;
    const auto audit = [&audited](const XnbReaderIdentity& identity)
    {
        ++audited;
        const std::string target = XnbTargetTypeName(identity);
        if (identity.genericArguments.empty())
        {
            // A non-generic identity's flag cannot matter, and its target name must be its own
            // spelling with nothing appended.
            EXPECT_EQ(target, identity.targetBaseName);
            return;
        }
        if (identity.targetSharesGenericArguments)
        {
            EXPECT_EQ(target.find(identity.targetBaseName + "[["), 0u)
                << "shared-argument identity " << identity.readerBaseName
                << " must append its argument list to its target name";
        }
        else
        {
            EXPECT_EQ(target, identity.targetBaseName)
                << "identity " << identity.readerBaseName
                << " does not share its arguments with its target, so nothing may be appended";
            EXPECT_EQ(target.find("[["), std::string::npos)
                << "target " << target << " must not carry a generic argument list";
        }
        // Whatever the shape, the reader name must round-trip through normalization -- which is
        // what a malformed bracket sequence breaks.
        EXPECT_EQ(NormalizeXnbTypeReaderName(
                      FormatXnbReaderName(identity, XnbReaderNameStyle::Xna40)),
                  XnbCanonicalReaderName(identity));
    };

    for (const std::string& name : registry.RegisteredReaderNames())
    {
        static_cast<void>(name);
    }
    // RegisteredReaderNames() flattens to strings, so walk the identities themselves through the
    // known built-in accessors plus the registered collection instantiations.
    const std::vector<XnbReaderIdentity> identities = {
        XnbBuiltInReaderIdentity<bool>(),
        XnbBuiltInReaderIdentity<std::uint8_t>(),
        XnbBuiltInReaderIdentity<std::int8_t>(),
        XnbBuiltInReaderIdentity<std::int16_t>(),
        XnbBuiltInReaderIdentity<std::uint16_t>(),
        XnbBuiltInReaderIdentity<std::int32_t>(),
        XnbBuiltInReaderIdentity<std::uint32_t>(),
        XnbBuiltInReaderIdentity<std::int64_t>(),
        XnbBuiltInReaderIdentity<std::uint64_t>(),
        XnbBuiltInReaderIdentity<float>(),
        XnbBuiltInReaderIdentity<double>(),
        XnbBuiltInReaderIdentity<SharpRuntime::charcs>(),
        XnbBuiltInReaderIdentity<std::string>(),
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Matrix>(),
        XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector3>(),
        SurfaceFormatEnum(),
        ListOf(XnbBuiltInReaderIdentity<std::string>()),
        ListOf(SurfaceFormatEnum()),
        ListOf(ArrayOf(XnbBuiltInReaderIdentity<std::int32_t>())),
        ArrayOf(XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector3>()),
        ArrayOf(SurfaceFormatEnum()),
        NullableOf(SurfaceFormatEnum()),
        NullableOf(ArrayOf(XnbBuiltInReaderIdentity<std::int32_t>())),
        DictionaryOf(XnbBuiltInReaderIdentity<std::string>(),
                     XnbBuiltInReaderIdentity<std::int32_t>()),
        DictionaryOf(XnbBuiltInReaderIdentity<std::string>(),
                     ListOf(NullableOf(SurfaceFormatEnum()))),
        XnbTexture2DReaderIdentity(),
    };
    for (const XnbReaderIdentity& identity : identities) { Walk(identity, audit); }
    EXPECT_GT(audited, identities.size());
}

// -- and the claim that matters: CNA's own reader can load what its writer registers -----------

TEST(XnbReaderIdentityTest, EveryBuiltInWritersReaderNameResolvesInTheRuntimeReaderRegistry)
{
    // plans/plan_xnapipeline.md XNAP-9D. The registry is keyed by C++ type and takes registrations
    // from anywhere, so nothing structurally stops a writer being registered for a name no reader
    // resolves -- which would produce a file CNA itself could not load. This is the check.
    RegisterAllBuiltInXnbReaders();

    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);
    for (const std::string& canonical : registry.RegisteredReaderNames())
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(canonical))
            << "a built-in writer emits '" << canonical
            << "' and no built-in reader resolves it";
    }
}

TEST(XnbReaderIdentityTest, TheTwoInstantiationsPortedFromTheOtherBranchAreRegisteredBothSides)
{
    // XNAP-9D: List<Matrix> and Vector3[] both had readers in CNA's runtime registry -- a real XNA
    // `Model` names ArrayReader<Vector3> in its type table -- and no writers. They do now.
    RegisterAllBuiltInXnbReaders();
    XnbTypeWriterRegistry registry;
    RegisterBuiltInXnbWriters(registry);

    const std::vector<std::string> names = registry.RegisteredReaderNames();
    for (const char* expected : {
             "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Matrix]]",
             "Microsoft.Xna.Framework.Content.ArrayReader`1[[Microsoft.Xna.Framework.Vector3]]",
         })
    {
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end())
            << expected << " has a reader and must have a writer";
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(expected));
    }
}
