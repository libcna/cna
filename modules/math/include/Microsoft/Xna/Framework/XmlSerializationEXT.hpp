// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file XmlSerializationEXT.hpp
 * @brief CNAEXT: opts the XNA math value types into
 * `System::Xml::Serialization::XmlSerializer`.
 *
 * .NET's `XmlSerializer` needs no help with these: it reflects over a type's public fields, so a
 * game that serializes a `Matrix` gets `<M11>`…`<M44>` and one that serializes a `Vector3` gets
 * `<X>`/`<Y>`/`<Z>` without writing anything. C++ has no reflection, so somebody has to state the
 * member list once — and that somebody is CNA rather than each game, because the shape is XNA's,
 * not any game's.
 *
 * The element names are the fields' own, which is what .NET writes for a member carrying no
 * `[XmlElement(ElementName=…)]`, and they are the names Microsoft's own shipped documents use —
 * `ShipGame/Content/levels/level1/level1_spawns.xml` for `Matrix`,
 * `level1_lights.xml` for `Vector3`, `Spacewar/settings.xml` for `Vector2` and `Vector4`.
 *
 * Header-only and opt-in: including it is what pulls in `SharpRuntime::Xml.Serialization`, so the
 * math module itself keeps no dependency on it. A consumer that includes this must link that
 * component.
 *
 * @note CNAEXT — not part of the XNA 4.0 API. XNA expresses the same thing through reflection.
 */

#include <tuple>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/Xml/Serialization/detail/XmlMember.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief CNAEXT: root element name XmlSerializer uses for a top-level Vector2. */
    CNAEXT constexpr const char* SharpXmlRootName(const Vector2*) { return "Vector2"; }

    /** @brief CNAEXT: Vector2's serialized members, in XNA's own field order. */
    CNAEXT constexpr auto SharpXmlMembers(const Vector2*)
    {
        using ::System::Xml::Serialization::detail::MakeMember;
        return std::make_tuple(MakeMember("X", &Vector2::X), MakeMember("Y", &Vector2::Y));
    }

    /** @brief CNAEXT: root element name XmlSerializer uses for a top-level Vector3. */
    CNAEXT constexpr const char* SharpXmlRootName(const Vector3*) { return "Vector3"; }

    /** @brief CNAEXT: Vector3's serialized members, in XNA's own field order. */
    CNAEXT constexpr auto SharpXmlMembers(const Vector3*)
    {
        using ::System::Xml::Serialization::detail::MakeMember;
        return std::make_tuple(MakeMember("X", &Vector3::X), MakeMember("Y", &Vector3::Y),
                               MakeMember("Z", &Vector3::Z));
    }

    /** @brief CNAEXT: root element name XmlSerializer uses for a top-level Vector4. */
    CNAEXT constexpr const char* SharpXmlRootName(const Vector4*) { return "Vector4"; }

    /** @brief CNAEXT: Vector4's serialized members, in XNA's own field order. */
    CNAEXT constexpr auto SharpXmlMembers(const Vector4*)
    {
        using ::System::Xml::Serialization::detail::MakeMember;
        return std::make_tuple(MakeMember("X", &Vector4::X), MakeMember("Y", &Vector4::Y),
                               MakeMember("Z", &Vector4::Z), MakeMember("W", &Vector4::W));
    }

    /** @brief CNAEXT: root element name XmlSerializer uses for a top-level Quaternion. */
    CNAEXT constexpr const char* SharpXmlRootName(const Quaternion*) { return "Quaternion"; }

    /** @brief CNAEXT: Quaternion's serialized members, in XNA's own field order. */
    CNAEXT constexpr auto SharpXmlMembers(const Quaternion*)
    {
        using ::System::Xml::Serialization::detail::MakeMember;
        return std::make_tuple(MakeMember("X", &Quaternion::X), MakeMember("Y", &Quaternion::Y),
                               MakeMember("Z", &Quaternion::Z), MakeMember("W", &Quaternion::W));
    }

    /** @brief CNAEXT: root element name XmlSerializer uses for a top-level Matrix. */
    CNAEXT constexpr const char* SharpXmlRootName(const Matrix*) { return "Matrix"; }

    /**
     * @brief CNAEXT: Matrix's sixteen serialized members, row by row.
     *
     * The order is the one .NET's reflection produces for `Matrix`'s declaration order, and it is
     * the order Microsoft's own shipped documents are written in.
     */
    CNAEXT constexpr auto SharpXmlMembers(const Matrix*)
    {
        using ::System::Xml::Serialization::detail::MakeMember;
        return std::make_tuple(
            MakeMember("M11", &Matrix::M11), MakeMember("M12", &Matrix::M12),
            MakeMember("M13", &Matrix::M13), MakeMember("M14", &Matrix::M14),
            MakeMember("M21", &Matrix::M21), MakeMember("M22", &Matrix::M22),
            MakeMember("M23", &Matrix::M23), MakeMember("M24", &Matrix::M24),
            MakeMember("M31", &Matrix::M31), MakeMember("M32", &Matrix::M32),
            MakeMember("M33", &Matrix::M33), MakeMember("M34", &Matrix::M34),
            MakeMember("M41", &Matrix::M41), MakeMember("M42", &Matrix::M42),
            MakeMember("M43", &Matrix::M43), MakeMember("M44", &Matrix::M44));
    }
}
