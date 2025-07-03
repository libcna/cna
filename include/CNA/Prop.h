#ifndef PROP_H

//d like define
//i like implement

#define DEF_MEMBER(type, name) \
    private: type name##_;

// #define ddata(type, name) \
//     DEF_PROP(type, name) \
//     /** This is C# like property, but using getter and setter.*/ \
//     public: [[nodiscard]] type get##name##Property() const; \
//     public: void set##name##Property(const type& v);
//
// #define dgetter(type, name) \
// DEF_PROP(type, name) \
// /** This is C# like readonly property, but using getter.*/ \
// public: [[nodiscard]] type get##name##Property() const;
//
// #define idata(type, name, class)\
// type class::get##name##Property() const { return name##_ ; } \
// void class::set##name##Property(const type& v) { name##_ = v; }
//
// #define igetter(type, name, class)\
// type class::get##name##Property() const { return name##_ ; }
//
//
// ////
// #define ddatastatic(type, name) \
// /** This is C# like property, but using getter and setter.*/ \
// public: [[nodiscard]] static type get##name##Property(); \
// public: static void set##name##Property(const type& v);
//
// #define dgetterstatic(type, name) \
// /** This is C# like readonly property, but using getter.*/ \
// public: [[nodiscard]] static type get##name##Property();
//
// #define idatastatic(type, name, class, init)\
// static type name##_ = init;\
// type class::get##name##Property() { return name##_ ; } \
// void class::set##name##Property(const type& v) { name##_ = v; }
//
// #define igetterstatic(type, name, class, init)\
// static type name##_ = init;\
// type class::get##name##Property() { return name##_ ; }
//




#define IF_1(prefix, code) IF_1_##prefix(code)
#define IF_0(prefix, code) IF_0_##prefix(code)

#define IF_1_property(code) code
#define IF_0_property(code)

#define IF_YES(prefix, cond, code) IF_##cond(prefix, code)

#define static1 static
#define static0
#define constret1 const
#define constret0
#define ref1 &
#define ref0
#define constmet1 const
#define constmet0
#define getter1 1
#define getter0 0
#define setter1 1
#define setter0 0
#define member1 1
#define member0 0
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

//def_prop(int, Test, 1, 1, 1, , , &)

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












namespace CNA {
}

#endif // PROP_H
