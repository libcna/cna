// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"

#include "openvg.h"

#include <stdexcept>

namespace CNA::Internal::Renderers::OpenVg
{
    namespace
    {
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
        // too -- see OpenVgSpriteBatchRenderer.cpp's ApplyDeviceFlip-area comment.
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

        VGImage img = vgCreateImage(VG_sABGR_8888, width_, height_,
                                    VG_IMAGE_QUALITY_FASTER | VG_IMAGE_QUALITY_BETTER);
        if (img == VG_INVALID_HANDLE)
            throw std::runtime_error("OpenVG (ShivaVG): vgCreateImage failed.");
        image_ = reinterpret_cast<void*>(img);

        if (!data.pixels.empty())
        {
            const int stride = width_ * 4;
            if (static_cast<std::size_t>(stride) * static_cast<std::size_t>(height_) > data.pixels.size())
                throw std::runtime_error("OpenVG (ShivaVG): ImageData.pixels is smaller than width*height*4.");
            UploadTopRowFirstRgba(image_, data.pixels.data(), width_, height_, stride);
        }
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
