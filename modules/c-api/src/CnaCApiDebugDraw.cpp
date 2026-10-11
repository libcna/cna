// SPDX-License-Identifier: MS-PL
#include "CNA/C/graphics_ext.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#ifdef CNA_CNAEXT
#include "CNA/Graphics/DebugDraw.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace {

using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
namespace Ext = CNA::Graphics;
using namespace CNA::C::Detail;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
struct DebugDrawResource final {
  std::shared_ptr<Ext::DebugDraw> value;
  CNA_Handle parentGame;
};
template <class T> CNA_Result StoreValue(T *out, T value) {
  if (!out)
    return InvalidArgument("Output is null.");
  *out = value;
  return CNA_RESULT_SUCCESS;
}
template <class T>
CNA_Result GetEngineResource(CNA_Handle h, ObjectKind k, const char *name,
                             std::shared_ptr<T> *out) {
  const auto r = GetRuntimeHandles().Get(h, k, out);
  return r == CNA_RESULT_SUCCESS
             ? r
             : Fail(r, ErrorCategoryForResult(r),
                    std::string(name) + " handle is invalid.");
}
template <class T, class F>
CNA_Result WithMap(CNA_Handle h, ObjectKind k, const char *name, F &&body) {
  return CallWithExceptionBarrier([&]() -> CNA_Result {
    std::shared_ptr<T> resource;
    if (const auto r = GetEngineResource(h, k, name, &resource);
        r != CNA_RESULT_SUCCESS)
      return r;
    return std::forward<F>(body)(resource);
  });
}
#define CNA_WITH_DEBUG(handle, body)                                           \
  WithMap<DebugDrawResource>(handle, ObjectKind::DebugDraw, "DebugDraw", body)
CNA_Vector3 Vec3(float x, float y, float z) { return {x, y, z}; }
Microsoft::Xna::Framework::Color ToNativeColour(CNA_Color v) {
  return {v.r, v.g, v.b, v.a};
}
CNA_Result RequireVector3Argument(const CNA_Vector3 *v, const char *msg) {
  return v ? CNA_RESULT_SUCCESS
           : InvalidArgument(msg);
}
CNA_Result ToNativeBounds(const CNA_BoundingBox *in, BoundingBox *out) {
  if (!in)
    return InvalidArgument("Box is null.");
  *out = BoundingBox(ToNative(in->min), ToNative(in->max));
  return CNA_RESULT_SUCCESS;
}
template <class T>
CNA_Result CopyValueRange(const std::vector<T> &src, T *dst, uint64_t cap,
                          uint64_t *count) {
  if (!count || (!dst && cap))
    return InvalidArgument("Count/copy arguments invalid.");
  *count = src.size();
  if (cap < src.size())
    return CNA_RESULT_BUFFER_TOO_SMALL;
  for (size_t i = 0; i < src.size(); ++i)
    dst[i] = src[i];
  return CNA_RESULT_SUCCESS;
}
} // namespace
CNA_Result cna_debug_draw_create(const CNA_Handle graphicsDeviceHandle,
                                 CNA_DebugDrawHandle *const outDebug) {
  return CallWithExceptionBarrier([&]() -> CNA_Result {
    if (outDebug == nullptr) {
      return InvalidArgument("The output handle is null.");
    }
    *outDebug = CNA_INVALID_HANDLE;
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
      return result;
    }
    const auto resource = std::make_shared<DebugDrawResource>(DebugDrawResource{
        std::make_shared<Ext::DebugDraw>(*graphicsDevice->value),
        graphicsDevice->parentGame});
    if (const CNA_Result result = CreateHandle(ObjectKind::DebugDraw, resource, outDebug);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
    return CNA_RESULT_SUCCESS;
  });
}

CNA_Result cna_debug_draw_destroy(const CNA_DebugDrawHandle debug) {
  return CallWithExceptionBarrier([&]() -> CNA_Result {
    std::shared_ptr<DebugDrawResource> resource;
    if (const CNA_Result result = GetEngineResource(
            debug, ObjectKind::DebugDraw, "DebugDraw", &resource);
        result != CNA_RESULT_SUCCESS) {
      return result;
    }
    if (const CNA_Result released = ReleaseHandle(debug);
        released != CNA_RESULT_SUCCESS) {
        return released;
    }
    RemoveOwnedGraphicsResourceFor(resource->parentGame);
    return CNA_RESULT_SUCCESS;
  });
}

CNA_Result cna_debug_draw_begin(const CNA_DebugDrawHandle debug,
                                const CNA_Matrix *const view,
                                const CNA_Matrix *const projection) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (view == nullptr || projection == nullptr) {
          return InvalidArgument("A matrix is null.");
        }
        d->value->begin(ToNative(*view), ToNative(*projection));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_end(const CNA_DebugDrawHandle debug) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        d->value->end();
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_clear(const CNA_DebugDrawHandle debug) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        d->value->clear();
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_add_line(const CNA_DebugDrawHandle debug,
                                   const CNA_Vector3 *const from,
                                   const CNA_Vector3 *const to,
                                   const CNA_Color colour) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (from == nullptr || to == nullptr) {
          return InvalidArgument("An endpoint is null.");
        }
        d->value->addLine(ToNative(*from), ToNative(*to),
                          ToNativeColour(colour));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_add_box(const CNA_DebugDrawHandle debug,
                                  const CNA_BoundingBox *const bounds,
                                  const CNA_Color colour) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        BoundingBox native;
        if (const CNA_Result result = ToNativeBounds(bounds, &native);
            result != CNA_RESULT_SUCCESS) {
          return result;
        }
        d->value->addBox(native, ToNativeColour(colour));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_add_sphere(const CNA_DebugDrawHandle debug,
                                     const CNA_Vector3 *const centre,
                                     const float radius, const CNA_Color colour,
                                     const int32_t segments) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(centre, "The centre is null.");
            result != CNA_RESULT_SUCCESS) {
          return result;
        }
        // Clamped by the canonical body, not refused here: a debug shape drawn
        // with too few or absurdly many segments is still a debug shape.
        d->value->addSphere(ToNative(*centre), radius,
                            ToNativeColour(colour), static_cast<int>(segments));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_add_bounding_sphere(
    const CNA_DebugDrawHandle debug, const CNA_BoundingSphere *const sphere,
    const CNA_Color colour, const int32_t segments) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (sphere == nullptr) {
          return InvalidArgument("The sphere is null.");
        }
        const BoundingSphere native(ToNative(sphere->center),
                                    sphere->radius);
        d->value->addSphere(native, ToNativeColour(colour),
                            static_cast<int>(segments));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_add_frustum(const CNA_DebugDrawHandle debug,
                                      const CNA_BoundingFrustum frustum,
                                      const CNA_Color colour) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        d->value->addFrustum(Microsoft::Xna::Framework::BoundingFrustum(
                                 ToNative(frustum.matrix)),
                             ToNativeColour(colour));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_add_cross(const CNA_DebugDrawHandle debug,
                                    const CNA_Vector3 *const position,
                                    const float size, const CNA_Color colour) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(position, "The position is null.");
            result != CNA_RESULT_SUCCESS) {
          return result;
        }
        d->value->addCross(ToNative(*position), size,
                           ToNativeColour(colour));
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_is_depth_tested(const CNA_DebugDrawHandle debug,
                                          CNA_Bool *const outDepthTested) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        return StoreValue(outDepthTested,
                          static_cast<CNA_Bool>(ToCBool(d->value->isDepthTested())));
      });
}

CNA_Result cna_debug_draw_set_depth_tested(const CNA_DebugDrawHandle debug,
                                           const CNA_Bool depthTested) {
  if (const CNA_Result result =
          CNA::C::Detail::ValidateCanonicalBool(depthTested, "depth_tested");
      result != CNA_RESULT_SUCCESS) {
    return result;
  }
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        d->value->setDepthTested(depthTested == CNA_TRUE);
        return CNA_RESULT_SUCCESS;
      });
}

CNA_Result cna_debug_draw_get_line_count(const CNA_DebugDrawHandle debug,
                                         int32_t *const outCount) {
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        return StoreValue(outCount,
                          static_cast<int32_t>(d->value->getLineCount()));
      });
}

CNA_Result cna_debug_draw_copy_vertices(
    const CNA_DebugDrawHandle debug, const CNA_Bool depthTested,
    CNA_VertexPositionColor *const destination, const uint64_t capacity,
    uint64_t *const outCount) {
  if (const CNA_Result result =
          CNA::C::Detail::ValidateCanonicalBool(depthTested, "depth_tested");
      result != CNA_RESULT_SUCCESS) {
    return result;
  }
  return CNA_WITH_DEBUG(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        const auto &vertices = d->value->getVertices(depthTested == CNA_TRUE);
        std::vector<CNA_VertexPositionColor> values;
        values.reserve(vertices.size());
        for (const auto &vertex : vertices) {
          CNA_VertexPositionColor value{};
          value.position =
              Vec3(vertex.Position.X, vertex.Position.Y, vertex.Position.Z);
          value.color.r = vertex.Color.getRProperty();
          value.color.g = vertex.Color.getGProperty();
          value.color.b = vertex.Color.getBProperty();
          value.color.a = vertex.Color.getAProperty();
          values.push_back(value);
        }
        return CopyValueRange(values, destination, capacity, outCount);
      });
}

#else
CNA_Result cna_debug_draw_create(CNA_Handle, CNA_DebugDrawHandle *) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_destroy(CNA_DebugDrawHandle) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_begin(CNA_DebugDrawHandle, const CNA_Matrix *,
                                const CNA_Matrix *) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_end(CNA_DebugDrawHandle) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_clear(CNA_DebugDrawHandle) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_add_line(CNA_DebugDrawHandle, const CNA_Vector3 *,
                                   const CNA_Vector3 *, CNA_Color) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_add_box(CNA_DebugDrawHandle, const CNA_BoundingBox *,
                                  CNA_Color) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_add_sphere(CNA_DebugDrawHandle, const CNA_Vector3 *,
                                     float, CNA_Color, int32_t) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_add_bounding_sphere(CNA_DebugDrawHandle,
                                              const CNA_BoundingSphere *,
                                              CNA_Color, int32_t) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_add_frustum(CNA_DebugDrawHandle, CNA_BoundingFrustum,
                                      CNA_Color) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_add_cross(CNA_DebugDrawHandle, const CNA_Vector3 *,
                                    float, CNA_Color) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_is_depth_tested(CNA_DebugDrawHandle, CNA_Bool *) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_set_depth_tested(CNA_DebugDrawHandle, CNA_Bool) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_get_line_count(CNA_DebugDrawHandle, int32_t *) {
  return CNA_RESULT_NOT_SUPPORTED;
}
CNA_Result cna_debug_draw_copy_vertices(CNA_DebugDrawHandle, CNA_Bool,
                                        CNA_VertexPositionColor *, uint64_t,
                                        uint64_t *) {
  return CNA_RESULT_NOT_SUPPORTED;
}
#endif // CNA_CNAEXT
