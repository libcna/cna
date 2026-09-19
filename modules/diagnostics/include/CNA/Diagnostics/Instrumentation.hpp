// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Diagnostics/Diagnostics.hpp"

#define CNA_DIAGNOSTICS_DETAIL_JOIN_INNER(a, b) a##b
#define CNA_DIAGNOSTICS_DETAIL_JOIN(a, b) CNA_DIAGNOSTICS_DETAIL_JOIN_INNER(a, b)
#if defined(__COUNTER__)
#define CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID __COUNTER__
#else
#define CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID __LINE__
#endif

// The arguments stay unevaluated, so instrumentation still costs nothing here, but `sizeof`
// keeps them compiled and type-checked. Discarding them outright would let a broken call site
// pass the default OFF build and only fail for whoever enables STATS or FULL.
#define CNA_DIAGNOSTICS_DETAIL_CHECK(...) do { (void) sizeof((__VA_ARGS__)); } while (false)

#if CNA_DIAGNOSTICS_LEVEL >= 1
#define CNA_DIAGNOSTICS_DETAIL_COUNTER_ADD(name, delta, identifier)                   \
    do                                                                                \
    {                                                                                 \
        static const ::CNA::Diagnostics::CounterHandle                                \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsCounter_, identifier){name};    \
        CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsCounter_, identifier).Add(delta);    \
    } while (false)
#define CNA_DIAGNOSTICS_COUNTER_ADD(name, delta)                                      \
    CNA_DIAGNOSTICS_DETAIL_COUNTER_ADD(name, delta, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#define CNA_DIAGNOSTICS_DETAIL_GAUGE_SET(name, value, identifier)                     \
    do                                                                                \
    {                                                                                 \
        static const ::CNA::Diagnostics::GaugeHandle                                  \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsGauge_, identifier){name};      \
        CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsGauge_, identifier).Set(value);      \
    } while (false)
#define CNA_DIAGNOSTICS_GAUGE_SET(name, value)                                        \
    CNA_DIAGNOSTICS_DETAIL_GAUGE_SET(name, value, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#define CNA_DIAGNOSTICS_DETAIL_GAUGE_ADD(name, delta, identifier)                     \
    do                                                                                \
    {                                                                                 \
        static const ::CNA::Diagnostics::GaugeHandle                                  \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsGauge_, identifier){name};      \
        CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsGauge_, identifier).Add(delta);      \
    } while (false)
#define CNA_DIAGNOSTICS_GAUGE_ADD(name, delta)                                        \
    CNA_DIAGNOSTICS_DETAIL_GAUGE_ADD(name, delta, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#define CNA_DIAGNOSTICS_DETAIL_FRAME_COUNTER_ADD(name, delta, identifier)             \
    do                                                                                \
    {                                                                                 \
        static const ::CNA::Diagnostics::FrameCounterHandle                           \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsFrameCounter_, identifier){name}; \
        CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsFrameCounter_, identifier).Add(delta); \
    } while (false)
#define CNA_DIAGNOSTICS_FRAME_COUNTER_ADD(name, delta)                                \
    CNA_DIAGNOSTICS_DETAIL_FRAME_COUNTER_ADD(                                         \
        name, delta, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#define CNA_DIAGNOSTICS_FRAME_SCOPE()                                                 \
    ::CNA::Diagnostics::FrameScope                                                    \
        CNA_DIAGNOSTICS_DETAIL_JOIN(                                                  \
            cnaDiagnosticsFrameScope_, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#else
#define CNA_DIAGNOSTICS_COUNTER_ADD(name, delta) CNA_DIAGNOSTICS_DETAIL_CHECK(name, delta)
#define CNA_DIAGNOSTICS_GAUGE_SET(name, value) CNA_DIAGNOSTICS_DETAIL_CHECK(name, value)
#define CNA_DIAGNOSTICS_GAUGE_ADD(name, delta) CNA_DIAGNOSTICS_DETAIL_CHECK(name, delta)
#define CNA_DIAGNOSTICS_FRAME_COUNTER_ADD(name, delta) \
    CNA_DIAGNOSTICS_DETAIL_CHECK(name, delta)
#define CNA_DIAGNOSTICS_FRAME_SCOPE() do { } while (false)
#endif

#if CNA_DIAGNOSTICS_LEVEL >= 2
#define CNA_DIAGNOSTICS_DETAIL_PROFILE_SCOPE(name, category, identifier)              \
    static const ::CNA::Diagnostics::NameHandle                                       \
        CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsName_, identifier){name};           \
    ::CNA::Diagnostics::ZoneScope                                                     \
        CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsZone_, identifier){                 \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsName_, identifier), category}
#define CNA_PROFILE_SCOPE_CATEGORY(name, category)                                    \
    CNA_DIAGNOSTICS_DETAIL_PROFILE_SCOPE(                                             \
        name, category, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#define CNA_PROFILE_SCOPE(name)                                                       \
    CNA_PROFILE_SCOPE_CATEGORY(name, ::CNA::Diagnostics::Category::Application)
#define CNA_DIAGNOSTICS_DETAIL_EVENT(name, category, identifier)                      \
    do                                                                                \
    {                                                                                 \
        static const ::CNA::Diagnostics::NameHandle                                   \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsEventName_, identifier){name};  \
        ::CNA::Diagnostics::MarkEvent(                                                \
            CNA_DIAGNOSTICS_DETAIL_JOIN(cnaDiagnosticsEventName_, identifier), category);\
    } while (false)
#define CNA_DIAGNOSTICS_EVENT_CATEGORY(name, category)                                \
    CNA_DIAGNOSTICS_DETAIL_EVENT(name, category, CNA_DIAGNOSTICS_DETAIL_UNIQUE_ID)
#define CNA_DIAGNOSTICS_EVENT(name)                                                   \
    CNA_DIAGNOSTICS_EVENT_CATEGORY(name, ::CNA::Diagnostics::Category::Application)
#else
#define CNA_PROFILE_SCOPE_CATEGORY(name, category) CNA_DIAGNOSTICS_DETAIL_CHECK(name, category)
#define CNA_PROFILE_SCOPE(name) CNA_DIAGNOSTICS_DETAIL_CHECK(name)
#define CNA_DIAGNOSTICS_EVENT_CATEGORY(name, category) \
    CNA_DIAGNOSTICS_DETAIL_CHECK(name, category)
#define CNA_DIAGNOSTICS_EVENT(name) CNA_DIAGNOSTICS_DETAIL_CHECK(name)
#endif
