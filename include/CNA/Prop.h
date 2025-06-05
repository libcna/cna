#ifndef PROP_H

//d like define
//i like implement

#define DEF_PROP(type, name) \
    private: type name##_;
#define ddata(type, name) \
    DEF_PROP(type, name) \
    /** This is C# like property, but using getter and setter.*/ \
    public: [[nodiscard]] type get##name##Property() const; \
    public: void set##name##Property(const type& v);

#define dgetter(type, name) \
DEF_PROP(type, name) \
/** This is C# like readonly property, but using getter.*/ \
public: [[nodiscard]] type get##name##Property() const;

#define idata(type, name, class)\
type class::get##name##Property() const { return name##_ ; } \
void class::set##name##Property(const type& v) { name##_ = v; }

#define igetter(type, name, class)\
type class::get##name##Property() const { return name##_ ; }


////
#define ddatastatic(type, name) \
/** This is C# like property, but using getter and setter.*/ \
public: [[nodiscard]] static type get##name##Property(); \
public: static void set##name##Property(const type& v);

#define dgetterstatic(type, name) \
/** This is C# like readonly property, but using getter.*/ \
public: [[nodiscard]] static type get##name##Property();

#define idatastatic(type, name, class, init)\
static type name##_ = init;\
type class::get##name##Property() { return name##_ ; } \
void class::set##name##Property(const type& v) { name##_ = v; }

#define igetterstatic(type, name, class, init)\
static type name##_ = init;\
type class::get##name##Property() { return name##_ ; } \


namespace CNA {
}

#endif // PROP_H
