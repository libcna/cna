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
  std::shared_ptr<T> resource;
  if (const auto r = GetEngineResource(h, k, name, &resource);
      r != CNA_RESULT_SUCCESS)
    return r;
  return std::forward<F>(body)(resource);
}
CNA_Result BorrowDebugDraw(CNA_DebugDrawHandle h,
                           std::shared_ptr<DebugDrawResource> *out) {
  return GetEngineResource(h, ObjectKind::DebugDraw, "DebugDraw", out);
}
template <class F> CNA_Result WithDebugDraw(CNA_DebugDrawHandle h, F &&body) {
  return WithMap<DebugDrawResource>(h, ObjectKind::DebugDraw, "DebugDraw",
                                    std::forward<F>(body));
}
CNA_Vector3 Vec3(float x, float y, float z) { return {x, y, z}; }
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

[[nodiscard]] CNA_Result DebugDrawCreate(const CNA_Handle graphicsDeviceHandle,
                                         CNA_DebugDrawHandle *const outDebug) {
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
}

[[nodiscard]] CNA_Result DebugDrawDestroy(const CNA_DebugDrawHandle debug) {
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
}

[[nodiscard]] CNA_Result DebugDrawBegin(const CNA_DebugDrawHandle debug,
                                        const CNA_Matrix *const view,
                                        const CNA_Matrix *const projection) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (view == nullptr || projection == nullptr) {
          return InvalidArgument("A matrix is null.");
        }
        d->value->begin(ToNative(*view), ToNative(*projection));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawAddLine(const CNA_DebugDrawHandle debug,
                                          const CNA_Vector3 *const from,
                                          const CNA_Vector3 *const to,
                                          const CNA_Color colour) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (from == nullptr || to == nullptr) {
          return InvalidArgument("An endpoint is null.");
        }
        d->value->addLine(ToNative(*from), ToNative(*to),
                          ToNative(colour));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawAddBox(const CNA_DebugDrawHandle debug,
                                         const CNA_BoundingBox *const bounds,
                                         const CNA_Color colour) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        BoundingBox native;
        if (const CNA_Result result = ToNativeBounds(bounds, &native);
            result != CNA_RESULT_SUCCESS) {
          return result;
        }
        d->value->addBox(native, ToNative(colour));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawAddSphere(const CNA_DebugDrawHandle debug,
                                            const CNA_Vector3 *const centre,
                                            const float radius, const CNA_Color colour,
                                            const int32_t segments) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(centre, "The centre is null.");
            result != CNA_RESULT_SUCCESS) {
          return result;
        }
        // Clamped by the canonical body, not refused here: a debug shape drawn
        // with too few or absurdly many segments is still a debug shape.
        d->value->addSphere(ToNative(*centre), radius,
                            ToNative(colour), static_cast<int>(segments));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawAddBoundingSphere(
    const CNA_DebugDrawHandle debug, const CNA_BoundingSphere *const sphere,
    const CNA_Color colour, const int32_t segments) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (sphere == nullptr) {
          return InvalidArgument("The sphere is null.");
        }
        const BoundingSphere native(ToNative(sphere->center),
                                    sphere->radius);
        d->value->addSphere(native, ToNative(colour),
                            static_cast<int>(segments));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawAddFrustum(const CNA_DebugDrawHandle debug,
                                             const CNA_BoundingFrustum frustum,
                                             const CNA_Color colour) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        d->value->addFrustum(Microsoft::Xna::Framework::BoundingFrustum(
                                 ToNative(frustum.matrix)),
                             ToNative(colour));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawAddCross(const CNA_DebugDrawHandle debug,
                                           const CNA_Vector3 *const position,
                                           const float size, const CNA_Color colour) {
  return WithDebugDraw(
      debug, [&](const std::shared_ptr<DebugDrawResource> &d) -> CNA_Result {
        if (const CNA_Result result =
                RequireVector3Argument(position, "The position is null.");
            result != CNA_RESULT_SUCCESS) {
          return result;
        }
        d->value->addCross(ToNative(*position), size,
                           ToNative(colour));
        return CNA_RESULT_SUCCESS;
      });
}

[[nodiscard]] CNA_Result DebugDrawCopyVertices(
    const CNA_DebugDrawHandle debug, const CNA_Bool depthTested,
    CNA_VertexPositionColor *const destination, const uint64_t capacity,
    uint64_t *const outCount) {
  return WithDebugDraw(
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

} // namespace

#endif // CNA_CNAEXT

// The routes generated from modules/c-api/spec/DebugDraw.toml.
#include "generated/DebugDraw.inc"
