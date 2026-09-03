// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"

#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

#if SHARP_RUNTIME_HAS_NATIVE_INT128
#include "System/Decimal.hpp"
#endif

namespace CNA::Internal::Xnb
{
    namespace
    {
        /**
         * @brief Builds one non-generic built-in reader identity.
         *
         * @param readerBaseName Assembly-free reader type name.
         * @param readerAssembly Assembly hosting the reader, or None when never qualified.
         * @param targetBaseName Assembly-free target type name.
         * @param targetAssembly Assembly hosting the target type.
         * @param evidence How this spelling was established.
         * @return The complete identity.
         */
        [[nodiscard]] XnbReaderIdentity Identity(
            const char* readerBaseName, const XnbAssembly readerAssembly,
            const char* targetBaseName, const XnbAssembly targetAssembly,
            const XnbNameEvidence evidence)
        {
            XnbReaderIdentity identity;
            identity.readerBaseName = readerBaseName;
            identity.readerAssembly = readerAssembly;
            identity.targetBaseName = targetBaseName;
            identity.targetAssembly = targetAssembly;
            identity.evidence = evidence;
            return identity;
        }

        /**
         * @brief Builds a `System.*` identity whose reader lives in `Microsoft.Xna.Framework`.
         *
         * @param readerBaseName Assembly-free reader type name.
         * @param targetBaseName Assembly-free `System.*` target type name.
         * @param evidence How this spelling was established.
         * @return The complete identity.
         */
        [[nodiscard]] XnbReaderIdentity SystemIdentity(
            const char* readerBaseName, const char* targetBaseName,
            const XnbNameEvidence evidence)
        {
            return Identity(readerBaseName, XnbAssembly::None, targetBaseName,
                            XnbAssembly::Mscorlib, evidence);
        }

        /**
         * @brief Builds a framework value-type identity whose reader lives in the core assembly.
         *
         * @param readerBaseName Assembly-free reader type name.
         * @param targetBaseName Assembly-free `Microsoft.Xna.Framework.*` target type name.
         * @param evidence How this spelling was established.
         * @return The complete identity.
         */
        [[nodiscard]] XnbReaderIdentity FrameworkIdentity(
            const char* readerBaseName, const char* targetBaseName,
            const XnbNameEvidence evidence)
        {
            return Identity(readerBaseName, XnbAssembly::None, targetBaseName,
                            XnbAssembly::Framework, evidence);
        }

        template<typename T>
        void AddFunctionWriter(XnbTypeWriterRegistry& registry, const bool serializedByReference,
                               void (*payload)(XnbWriter&, const T&))
        {
            registry.Register(std::make_shared<const XnbFunctionTypeWriter<T>>(
                XnbBuiltInReaderIdentity<T>(), serializedByReference, payload));
        }
    }

    // -- built-in reader identities (plans/plan_xnapipeline.md §2.5) ---------------------------
    //
    // Every `MonoGameFixture`/`Xna40Fixture` entry below was read out of a committed .xnb's own
    // type-reader table. The `DerivedRule` entries apply the rule those fixtures establish -- a
    // reader is assembly-qualified exactly when it does not live in Microsoft.Xna.Framework -- to
    // a type no committed fixture happens to exercise.

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<bool>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.BooleanReader", "System.Boolean",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint8_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.ByteReader", "System.Byte",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::int8_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.SByteReader", "System.SByte",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::int16_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.Int16Reader", "System.Int16",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint16_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.UInt16Reader", "System.UInt16",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::int32_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.Int32Reader", "System.Int32",
                              XnbNameEvidence::MonoGameFixture);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint32_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.UInt32Reader", "System.UInt32",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::int64_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.Int64Reader", "System.Int64",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::uint64_t>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.UInt64Reader", "System.UInt64",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<float>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.SingleReader", "System.Single",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<double>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.DoubleReader", "System.Double",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<SharpRuntime::charcs>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.CharReader", "System.Char",
                              XnbNameEvidence::MonoGameFixture);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<std::string>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.StringReader", "System.String",
                              XnbNameEvidence::Xna40Fixture);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<System::TimeSpan>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.TimeSpanReader", "System.TimeSpan",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<System::DateTime>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.DateTimeReader", "System.DateTime",
                              XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector2>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.Vector2Reader",
                                 "Microsoft.Xna.Framework.Vector2",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector3>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.Vector3Reader",
                                 "Microsoft.Xna.Framework.Vector3",
                                 XnbNameEvidence::MonoGameFixture);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector4>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.Vector4Reader",
                                 "Microsoft.Xna.Framework.Vector4",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Matrix>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.MatrixReader",
                                 "Microsoft.Xna.Framework.Matrix",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Quaternion>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.QuaternionReader",
                                 "Microsoft.Xna.Framework.Quaternion",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Color>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.ColorReader",
                                 "Microsoft.Xna.Framework.Color",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Point>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.PointReader",
                                 "Microsoft.Xna.Framework.Point",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Rectangle>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.RectangleReader",
                                 "Microsoft.Xna.Framework.Rectangle",
                                 XnbNameEvidence::MonoGameFixture);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Plane>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.PlaneReader",
                                 "Microsoft.Xna.Framework.Plane",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::BoundingBox>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.BoundingBoxReader",
                                 "Microsoft.Xna.Framework.BoundingBox",
                                 XnbNameEvidence::DerivedRule);
    }

    template<>
    XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::BoundingSphere>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.BoundingSphereReader",
                                 "Microsoft.Xna.Framework.BoundingSphere",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Ray>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.RayReader",
                                 "Microsoft.Xna.Framework.Ray",
                                 XnbNameEvidence::DerivedRule);
    }

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<
        Microsoft::Xna::Framework::BoundingFrustum>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.BoundingFrustumReader",
                                 "Microsoft.Xna.Framework.BoundingFrustum",
                                 XnbNameEvidence::DerivedRule);
    }

#if SHARP_RUNTIME_HAS_NATIVE_INT128
    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<System::Decimal>()
    {
        return SystemIdentity("Microsoft.Xna.Framework.Content.DecimalReader", "System.Decimal",
                              XnbNameEvidence::DerivedRule);
    }
#endif

    template<> XnbReaderIdentity XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Curve>()
    {
        return FrameworkIdentity("Microsoft.Xna.Framework.Content.CurveReader",
                                 "Microsoft.Xna.Framework.Curve",
                                 XnbNameEvidence::DerivedRule);
    }

    void RegisterBuiltInPrimitiveXnbWriters(XnbTypeWriterRegistry& registry)
    {
        AddFunctionWriter<bool>(registry, false,
            [](XnbWriter& output, const bool& value) { output.WriteBoolean(value); });
        AddFunctionWriter<std::uint8_t>(registry, false,
            [](XnbWriter& output, const std::uint8_t& value) { output.WriteByte(value); });
        AddFunctionWriter<std::int8_t>(registry, false,
            [](XnbWriter& output, const std::int8_t& value) { output.WriteSByte(value); });
        AddFunctionWriter<std::int16_t>(registry, false,
            [](XnbWriter& output, const std::int16_t& value) { output.WriteInt16(value); });
        AddFunctionWriter<std::uint16_t>(registry, false,
            [](XnbWriter& output, const std::uint16_t& value) { output.WriteUInt16(value); });
        AddFunctionWriter<std::int32_t>(registry, false,
            [](XnbWriter& output, const std::int32_t& value) { output.WriteInt32(value); });
        AddFunctionWriter<std::uint32_t>(registry, false,
            [](XnbWriter& output, const std::uint32_t& value) { output.WriteUInt32(value); });
        AddFunctionWriter<std::int64_t>(registry, false,
            [](XnbWriter& output, const std::int64_t& value) { output.WriteInt64(value); });
        AddFunctionWriter<std::uint64_t>(registry, false,
            [](XnbWriter& output, const std::uint64_t& value) { output.WriteUInt64(value); });
        AddFunctionWriter<float>(registry, false,
            [](XnbWriter& output, const float& value) { output.WriteSingle(value); });
        AddFunctionWriter<double>(registry, false,
            [](XnbWriter& output, const double& value) { output.WriteDouble(value); });
        AddFunctionWriter<SharpRuntime::charcs>(registry, false,
            [](XnbWriter& output, const SharpRuntime::charcs& value) { output.WriteChar(value); });
        AddFunctionWriter<std::string>(registry, true,
            [](XnbWriter& output, const std::string& value) { output.WriteString(value); });
#if SHARP_RUNTIME_HAS_NATIVE_INT128
        // Four Int32 words -- lo, mid, hi, flags -- exactly the layout
        // System::IO::BinaryReader::ReadDecimal() consumes, so the two are exact inverses. The
        // guard matches the reader's own: sharp-runtime provides System::Decimal only where it
        // has native 128-bit integers, and DecimalReader is registered under the same condition.
        AddFunctionWriter<System::Decimal>(registry, false,
            [](XnbWriter& output, const System::Decimal& value)
            {
                SharpRuntime::intcs lo = 0;
                SharpRuntime::intcs mid = 0;
                SharpRuntime::intcs hi = 0;
                SharpRuntime::intcs flags = 0;
                System::Decimal::GetBits(value, lo, mid, hi, flags);
                output.WriteInt32(static_cast<std::int32_t>(lo));
                output.WriteInt32(static_cast<std::int32_t>(mid));
                output.WriteInt32(static_cast<std::int32_t>(hi));
                output.WriteInt32(static_cast<std::int32_t>(flags));
            });
#endif
        AddFunctionWriter<System::TimeSpan>(registry, false,
            [](XnbWriter& output, const System::TimeSpan& value)
            {
                output.WriteInt64(value.getTicksProperty());
            });
        AddFunctionWriter<System::DateTime>(registry, false,
            [](XnbWriter& output, const System::DateTime& value)
            {
                // The top two bits carry DateTimeKind. System::DateTime does not model it, so CNA
                // writes Unspecified (0) rather than inventing a kind the value never had; the
                // reader masks the field out for exactly the same reason.
                output.WriteUInt64(static_cast<std::uint64_t>(value.getTicksProperty()));
            });
    }

    void RegisterBuiltInMathXnbWriters(XnbTypeWriterRegistry& registry)
    {
        using namespace Microsoft::Xna::Framework;

        AddFunctionWriter<Vector2>(registry, false,
            [](XnbWriter& output, const Vector2& value) { output.WriteVector2(value); });
        AddFunctionWriter<Vector3>(registry, false,
            [](XnbWriter& output, const Vector3& value) { output.WriteVector3(value); });
        AddFunctionWriter<Vector4>(registry, false,
            [](XnbWriter& output, const Vector4& value) { output.WriteVector4(value); });
        AddFunctionWriter<Matrix>(registry, false,
            [](XnbWriter& output, const Matrix& value) { output.WriteMatrix(value); });
        AddFunctionWriter<Quaternion>(registry, false,
            [](XnbWriter& output, const Quaternion& value) { output.WriteQuaternion(value); });
        AddFunctionWriter<Color>(registry, false,
            [](XnbWriter& output, const Color& value) { output.WriteColor(value); });
        AddFunctionWriter<Point>(registry, false,
            [](XnbWriter& output, const Point& value)
            {
                output.WriteInt32(value.X);
                output.WriteInt32(value.Y);
            });
        AddFunctionWriter<Rectangle>(registry, false,
            [](XnbWriter& output, const Rectangle& value) { output.WriteRectangle(value); });
        AddFunctionWriter<Plane>(registry, false,
            [](XnbWriter& output, const Plane& value)
            {
                output.WriteVector3(value.Normal);
                output.WriteSingle(value.D);
            });
        AddFunctionWriter<BoundingBox>(registry, false,
            [](XnbWriter& output, const BoundingBox& value)
            {
                output.WriteVector3(value.Min);
                output.WriteVector3(value.Max);
            });
        AddFunctionWriter<BoundingSphere>(registry, false,
            [](XnbWriter& output, const BoundingSphere& value)
            {
                output.WriteBoundingSphere(value);
            });
        AddFunctionWriter<Ray>(registry, false,
            [](XnbWriter& output, const Ray& value)
            {
                output.WriteVector3(value.Position);
                output.WriteVector3(value.Direction);
            });
        // A BoundingFrustum is a .NET *class*, unlike every other type in this group, so it is
        // written by reference; its payload is the view-projection matrix it was built from,
        // which is the only state the reader reconstructs it out of.
        AddFunctionWriter<BoundingFrustum>(registry, true,
            [](XnbWriter& output, const BoundingFrustum& value)
            {
                output.WriteMatrix(value.getMatrixProperty());
            });
        AddFunctionWriter<Curve>(registry, true,
            [](XnbWriter& output, const Curve& value)
            {
                output.WriteInt32(static_cast<std::int32_t>(value.getPreLoopProperty()));
                output.WriteInt32(static_cast<std::int32_t>(value.getPostLoopProperty()));
                const CurveKeyCollection& keys = value.getKeysProperty();
                const int count = keys.getCountProperty();
                output.RequireCollectionCount(static_cast<std::size_t>(count), "CurveWriter");
                output.WriteInt32(count);
                for (int index = 0; index < count; ++index)
                {
                    const CurveKey& key = keys[index];
                    output.WriteSingle(key.getPositionProperty());
                    output.WriteSingle(key.getValueProperty());
                    output.WriteSingle(key.getTangentInProperty());
                    output.WriteSingle(key.getTangentOutProperty());
                    output.WriteInt32(static_cast<std::int32_t>(key.getContinuityProperty()));
                }
            });
    }

    void RegisterBuiltInCollectionXnbWriters(XnbTypeWriterRegistry& registry)
    {
        // Only the closed generic instantiations CNA's own runtime reader registry already
        // resolves are registered here, so a file this writer produces always has a reader on the
        // other side. A consumer that needs another instantiation registers it explicitly; that is
        // the documented extension point, not a gap.
        registry.Register(std::make_shared<const XnbListTypeWriter<std::string>>(
            XnbBuiltInReaderIdentity<std::string>()));
        registry.Register(std::make_shared<const XnbListTypeWriter<std::int32_t>>(
            XnbBuiltInReaderIdentity<std::int32_t>()));
        registry.Register(std::make_shared<const XnbListTypeWriter<SharpRuntime::charcs>>(
            XnbBuiltInReaderIdentity<SharpRuntime::charcs>()));
        registry.Register(
            std::make_shared<const XnbListTypeWriter<Microsoft::Xna::Framework::Rectangle>>(
                XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Rectangle>()));
        registry.Register(
            std::make_shared<const XnbListTypeWriter<Microsoft::Xna::Framework::Vector3>>(
                XnbBuiltInReaderIdentity<Microsoft::Xna::Framework::Vector3>()));
        registry.Register(
            std::make_shared<const XnbDictionaryTypeWriter<std::string, std::int32_t>>(
                XnbBuiltInReaderIdentity<std::string>(),
                XnbBuiltInReaderIdentity<std::int32_t>()));
    }

    void RegisterBuiltInXnbWriters(XnbTypeWriterRegistry& registry)
    {
        RegisterBuiltInPrimitiveXnbWriters(registry);
        RegisterBuiltInMathXnbWriters(registry);
        RegisterBuiltInCollectionXnbWriters(registry);
        RegisterBuiltInAssetXnbWriters(registry);
    }

    const XnbTypeWriterRegistry& BuiltInXnbWriterRegistry()
    {
        static const XnbTypeWriterRegistry& registry = []() -> const XnbTypeWriterRegistry&
        {
            static XnbTypeWriterRegistry shared;
            RegisterBuiltInXnbWriters(shared);
            shared.Freeze();
            return shared;
        }();
        return registry;
    }
}
