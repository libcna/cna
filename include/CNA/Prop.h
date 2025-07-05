#ifndef PROP_H

//d like define
//i like implement

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
 *
 * @param type
 * @param name
 * @param getter 1 or 0
 * @param setter 1 or 0
 * @param member 1 or 0
 * @param static_keyword static or empty
 * @param const_return_qualifier  const or empty
 * @param ref_return_qualifier & or empty
 * @param const_method_qualifier const or empty
 * @example DEF_PROP(Vector3, Acceleration, getter1, setter0, member1, static0, constret1, ref1, constmet1)
 */
#define DEF_PROP(type, name, getter, setter, member, static_keyword, const_return_qualifier, ref_return_qualifier, const_method_qualifier) \
IF_YES(property, member, DEF_MEMBER(type, name)) \
IF_YES(property, getter, public: [[nodiscard]] static_keyword const_return_qualifier type ref_return_qualifier get##name##Property() const_method_qualifier;) \
IF_YES(property, setter, public: static_keyword void set##name##Property(const type& v);) \
IF_YES(property, setter, public: static_keyword void set##name##Property(type&& v);)

#define DDATA(type, name) \
DEF_PROP(type, name, getter1, setter1, member1, static0, constret1, ref1, constmet1)

#define DGETTER(type, name) \
DEF_PROP(type, name, getter1, setter0, member1, static0, constret1, ref1, constmet0)

#define DGETTERSTATIC(type, name) \
DEF_PROP(type, name, getter1, setter0, member1, static1, constret1, ref1, constmet0)

/**
DEF_PROP(Vector3, Acceleration, getter1, setter0, member1, static0, constret1, ref1, constmet1)
IMPL_PROP(Vector3, Acceleration, getter1, setter0, member0, static0, constret1, ref1, constmet1, AccelerometerReading)
 */
#define HAS_BODY_IMPL(...) GET_5TH_ARG(__VA_ARGS__, 1, 1, 1, 1, 0)
#define GET_5TH_ARG(_1, _2, _3, _4, _5, ...) _5

#define HAS_BODY(...) HAS_BODY_IMPL(__VA_ARGS__, 0, 0, 0, 0, 0)

/**
 *
 * @param type
 * @param name
 * @param getter 1 or 0
 * @param setter 1 or 0
 * @param member 1 or 0
 * @param static_keyword static or empty
 * @param const_return_qualifier  const or empty
 * @param ref_return_qualifier & or empty
 * @parem const_method_qualifier const or empty
 * @example IMPL_PROP(Vector3, Acceleration, getter1, setter0, member0, static0, constret1, ref1, constmet1, AccelerometerReading)
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

#define IDATA(type, name, clazz) \
IMPL_PROP(type, name, getter1, setter1, member0, static0, constret1, ref1, constmet1, clazz, nothing)

#define IGETTER(type, name, clazz) \
IMPL_PROP(type, name, getter1, setter0, member0, static0, constret1, ref1, constmet1, clazz, nothing)

#define IGETTERSTATIC(type, name, clazz, init) \
IMPL_PROP(type, name, getter1, setter0, member0, static1, constret1, ref1, constmet1, clazz, nothing)












namespace CNA {
}

#endif // PROP_H
