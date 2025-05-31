#ifndef PROP_H

//d like define
//i like implement

#define DEF_PROP(type, name, init) \
    private: type name##Property_ = init;
#define ddata(type, name, init) \
    DEF_PROP(type, name, init) \
    public: type name##Property(); \
    public: void name##Property(type v);

#define dgetter(type, name, init) \
DEF_PROP(type, name, init) \
public: type name##Property();

#define idata(type, name, class)\
type class##::name##Property() { return name##Property_ ; } \
void class##::name##Property(type v) { name##Property_ = v; }

#define igetter(type, name, class)\
type class##::name##Property() { return name##Property_ ; }

namespace CNA {
}

#endif // PROP_H
