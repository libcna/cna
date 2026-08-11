// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"

#include "openvg.h"

#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::OpenVg
{
    namespace
    {
        std::string DescribeVgError()
        {
            switch (vgGetError())
            {
                case VG_NO_ERROR: return "VG_NO_ERROR";
                case VG_BAD_HANDLE_ERROR: return "VG_BAD_HANDLE_ERROR";
                case VG_ILLEGAL_ARGUMENT_ERROR: return "VG_ILLEGAL_ARGUMENT_ERROR";
                case VG_OUT_OF_MEMORY_ERROR: return "VG_OUT_OF_MEMORY_ERROR";
                case VG_UNSUPPORTED_IMAGE_FORMAT_ERROR: return "VG_UNSUPPORTED_IMAGE_FORMAT_ERROR";
                default: return "unknown VGErrorCode";
            }
        }

        // Format choice: VG_sABGR_8888, not the more obviously-named VG_sRGBA_8888. ShivaVG's own
        // shSetupImageFormat (src/shImage.c) maps VG_sRGBA_8888 to `glformat=GL_RGBA,
        // gltype=GL_UNSIGNED_INT_8_8_8_8` (non-REV) -- on this little-endian host, reading 4
        // consecutive memory bytes as that packed 32-bit type actually assigns them (byte0..byte3)
        // to A,B,G,R, not R,G,B,A (found empirically: a straight R,G,B,A upload rendered as
        // magenta instead of green). VG_sABGR_8888 maps to `GL_RGBA` +
        // `GL_UNSIGNED_INT_8_8_8_8_REV`, whose REV packing assigns memory byte0..byte3 to R,G,B,A
        // on this same little-endian host -- exactly ImageData's own convention, with no CPU-side
        // byte swizzle needed.
        //
        // Uploaded WITHOUT any row reversal, straight positive stride: OpenVgSpriteBatchRenderer's
        // own image-user-to-surface matrix (device flip + per-sprite transform) already accounts
        // for OpenVG's Y-up image/surface space end to end -- ShivaVG's vgDrawImage generates
        // texture coordinates from the pre-transform (untransformed) object-space quad via
        // GL_OBJECT_LINEAR texgen, so a row reversal baked in at UPLOAD time only cancels out
        // correctly for a NON-rotated draw (found empirically: a pre-flipped upload combined with
        // the renderer's own device flip produced a vertically-mirrored image even with zero
        // rotation, since texcoord generation is independent of the modelview/rotation matrix).
        // Keeping the upload in ImageData's own top-row-first order and letting the draw-time
        // matrix be the ONLY place orientation is decided is what makes this work under rotation
        // too -- see OpenVgSpriteBatchRenderer.cpp's ApplyDeviceFlip-area comment. Proven directly
        // by openvg_texture_orientation_test.cpp's asymmetric-texel pixel oracle (P1-3).
        void UploadTopRowFirstRgba(void* image, const uint8_t* rgba, int width, int height, int strideBytes)
        {
            vgImageSubData(reinterpret_cast<VGImage>(image), rgba, strideBytes,
                           VG_sABGR_8888, 0, 0, width, height);
        }
    }

    OpenVgTextureRenderer::OpenVgTextureRenderer(const ImageData& data)
        : width_(data.width)
        , height_(data.height)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("OpenVG (ShivaVG): texture width/height must be positive.");

        // P1-2: validate every CPU-side argument BEFORE allocating the VGImage. This used to
        // happen after vgCreateImage -- a throw there left a real, now-unreachable VGImage handle
        // with nothing to free it (this object's destructor never runs for a not-yet-constructed
        // instance), a genuine per-failed-upload leak.
        const int stride = width_ * 4;
        if (!data.pixels.empty() &&
            static_cast<std::size_t>(stride) * static_cast<std::size_t>(height_) > data.pixels.size())
        {
            throw std::runtime_error("OpenVG (ShivaVG): ImageData.pixels is smaller than width*height*4.");
        }

        VGImage img = vgCreateImage(VG_sABGR_8888, width_, height_,
                                    VG_IMAGE_QUALITY_FASTER | VG_IMAGE_QUALITY_BETTER);
        if (img == VG_INVALID_HANDLE)
        {
            throw std::runtime_error(
                "OpenVG (ShivaVG): vgCreateImage failed (" + DescribeVgError() + ").");
        }
        // Owned from this point on: this->image_ is set immediately after the only OpenVG
        // resource-creation call in this constructor, so any later throw still runs the
        // destructor (which frees it) instead of leaking it.
        image_ = reinterpret_cast<void*>(img);

        if (!data.pixels.empty())
            UploadTopRowFirstRgba(image_, data.pixels.data(), width_, height_, stride);
    }

    OpenVgTextureRenderer::~OpenVgTextureRenderer()
    {
        if (image_)
            vgDestroyImage(reinterpret_cast<VGImage>(image_));
    }

    void OpenVgTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!image_ || !rgba) return;
        UploadTopRowFirstRgba(image_, rgba, width_, height_, stride);
    }
}
