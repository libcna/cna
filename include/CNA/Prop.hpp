#pragma once

#include <utility>

/**
 * @file Prop.hpp
 * @brief Macro helpers for simple member-backed properties.
 *
 * Typical usage:
 *
 * @code
 * class Texture {
 *     DDATA(int, Width)
 *     DGETTER(int, Height)
 * };
 *
 * // In the corresponding .cpp file:
 * IDATA(int, Width, Texture)
 * IGETTER(int, Height, Texture)
 * @endcode
 *
 * Static getter usage:
 *
 * @code
 * class Guide {
 *     DGETTERSTATIC(bool, IsTrialMode)
 * };
 *
 * // In the corresponding .cpp file:
 * IGETTERSTATIC(bool, IsTrialMode, Guide, false)
 * @endcode
 *
 * Naming convention:
 * - backing field: @c name_
 * - getter: @c getNameProperty()
 * - setter: @c setNameProperty(...)
 *
 * Public convenience macros:
 * - @c DDATA / @c IDATA          : read-write property
 * - @c DGETTER / @c IGETTER      : read-only property
 * - @c DGETTERSTATIC / @c IGETTERSTATIC : static read-only property
 *
 * @note Internal helper macros in this file are part of the implementation
 * and are usually not intended for direct use.
 */

// declare
#define DEF_MEMBER(type, name) \
    private: type name##_;

#define IF_1(prefix, code) IF_1_##prefix(code)
#define IF_0(prefix, code) IF_0_##prefix(code)

#define IF_1_property(code) code
#define IF_0_property(code)

#define IF_YES(prefix, cond, code) IF_##cond(prefix, code)

#define YES 1
#define NO 0
#define static1 static
#define static0
#define constret1 const
#define constret0
#define ref1 &
#define ref0
#define constmet1 const
#define constmet0
#define getter1 YES
#define getter0 NO
#define setter1 YES
#define setter0 NO
#define member1 YES
#define member0 NO
#define nothing

/**
 * @brief Internal low-level property declaration macro.
 *
 * Usually prefer one of:
 * - @c DDATA
 * - @c DGETTER
 * - @c DGETTERSTATIC
 */
#define DEF_PROP(type, name, getter, setter, member, static_keyword, const_return_qualifier, ref_return_qualifier, const_method_qualifier) \
IF_YES(property, member, DEF_MEMBER(type, name)) \
IF_YES(property, getter, public: [[nodiscard]] static_keyword const_return_qualifier type ref_return_qualifier get##name##Property() const_method_qualifier;) \
IF_YES(property, setter, public: static_keyword void set##name##Property(const type& v);) \
IF_YES(property, setter, public: static_keyword void set##name##Property(type&& v);)

/**
 * @brief Declares a read-write property with a backing member.
 *
 * Example:
 * @code
 * class Texture {
 *     DDATA(int, Width)
 * };
 * @endcode
 */
#define DDATA(type, name) \
DEF_PROP(type, name, getter1, setter1, member1, static0, constret1, ref1, constmet1)

/**
 * @brief Declares a read-only property with a backing member.
 *
 * Example:
 * @code
 * class Texture {
 *     DGETTER(int, Height)
 * };
 * @endcode
 */
#define DGETTER(type, name) \
DEF_PROP(type, name, getter1, setter0, member1, static0, constret1, ref1, constmet1)

/**
 * @brief Declares a static read-only property.
 *
 * Example:
 * @code
 * class Guide {
 *     DGETTERSTATIC(bool, IsTrialMode)
 * };
 * @endcode
 */
#define DGETTERSTATIC(type, name) \
DEF_PROP(type, name, getter1, setter0, member1, static1, constret1, ref1, constmet0)

/**
 * @brief Internal low-level property implementation macro.
 *
 * Usually prefer one of:
 * - @c IDATA
 * - @c IGETTER
 * - @c IGETTERSTATIC
 */
#define IMPL_PROP(\
    type, name, getter, setter, member, static_keyword, const_return_qualifier, \
    ref_return_qualifier, const_method_qualifier, clazz, member_static_init\
) \
IF_YES(property, member, static_keyword type name##_ = member_static_init;) \
IF_YES(property, getter, \
const_return_qualifier type ref_return_qualifier clazz::get##name##Property() const_method_qualifier { \
return name##_; \
}) \
IF_YES(property, setter, \
void clazz::set##name##Property(const type& v) { \
name##_ = v; \
}) \
IF_YES(property, setter, \
void clazz::set##name##Property(type&& v) { \
name##_ = std::move(v); \
})

/**
 * @brief Implements a read-write property declared with @c DDATA.
 *
 * Example:
 * @code
 * IDATA(int, Width, Texture)
 * @endcode
 */
#define IDATA(type, name, clazz) \
IMPL_PROP(type, name, getter1, setter1, member0, static0, constret1, ref1, constmet1, clazz, nothing)

/**
 * @brief Implements a read-only property declared with @c DGETTER.
 *
 * Example:
 * @code
 * IGETTER(int, Height, Texture)
 * @endcode
 */
#define IGETTER(type, name, clazz) \
IMPL_PROP(type, name, getter1, setter0, member0, static0, constret1, ref1, constmet1, clazz, nothing)

/**
 * @brief Implements a static read-only property declared with @c DGETTERSTATIC.
 *
 * Example:
 * @code
 * IGETTERSTATIC(bool, IsTrialMode, Guide, false)
 * @endcode
 *
 * @param init Initial value of the static backing storage.
 */
#define IGETTERSTATIC(type, name, clazz, init) \
IMPL_PROP(type, name, getter1, setter0, member1, static1, constret1, ref1, constmet0, clazz, init)

namespace CNA {
}