// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredLightBuffer.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::ShaderEffect;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        /// One 32-bit value as the four bytes of a texel, least significant first. The inverse of
        /// cnaUnpackUint in the emitted GLSL, and the only place the byte order is decided.
        Color PackUInt32(const std::uint32_t bits)
        {
            return Color(static_cast<int>( bits        & 0xFFu),
                         static_cast<int>((bits >>  8) & 0xFFu),
                         static_cast<int>((bits >> 16) & 0xFFu),
                         static_cast<int>((bits >> 24) & 0xFFu));
        }

        Color PackFloat(const float value)
        {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return PackUInt32(bits);
        }

        Vector3 Normalized(const Vector3& v, const Vector3& fallback)
        {
            const float length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
            if (!(length > 1e-6f)) return fallback;
            return Vector3(v.X / length, v.Y / length, v.Z / length);
        }

        int RowsFor(const int texels, const int width)
        {
            return std::max(1, (texels + width - 1) / width);
        }

    } // namespace

    ClusteredLightBuffer::ClusteredLightBuffer(GraphicsDevice& device) : device_(device) {}

    ClusteredLightBuffer::~ClusteredLightBuffer() = default;

    void ClusteredLightBuffer::upload(const ClusteredLightSetEXT& lights,
                                      const ClusteredLightGrid& grid,
                                      const ClusteredLightAssignment& assignment)
    {
        if (assignment.getLightCount() != lights.getCount() ||
            assignment.getClusterCount() != grid.getClusterCount())
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredLightBuffer::upload: the assignment describes a different "
                "set of lights or a different grid -- its light indices are positions in the set "
                "and its cluster indices are positions in the grid, so uploading a mismatched "
                "trio would light the wrong objects with the wrong lamps rather than fail");

        lightCount_     = lights.getCount();
        clusterCount_   = grid.getClusterCount();
        referenceCount_ = assignment.getTotalReferenceCount();
        tilesX_     = grid.getTilesX();
        tilesY_     = grid.getTilesY();
        sliceCount_ = grid.getSliceCount();
        nearPlane_  = grid.getNearPlane();
        farPlane_   = grid.getFarPlane();

        // ── The lights ───────────────────────────────────────────────────────
        {
            const int rows = std::max(1, lightCount_);
            std::vector<Color> texels(static_cast<std::size_t>(kFloatsPerLight) * rows,
                                      Color(0, 0, 0, 0));
            for (int index = 0; index < lightCount_; ++index)
            {
                const ClusteredLightEXT& light = lights.getAt(index);
                const Vector3 direction = Normalized(light.Direction, Vector3(0.0f, -1.0f, 0.0f));
                // Intensity is folded into the colour here rather than carried separately: the
                // shader only ever wants the product, and one fewer float is one fewer chance for
                // the two to be applied in different places.
                const Vector3 emitted(light.Color.X * light.Intensity,
                                      light.Color.Y * light.Intensity,
                                      light.Color.Z * light.Intensity);

                const float values[kFloatsPerLight] = {
                    light.Position.X, light.Position.Y, light.Position.Z, light.Range,
                    emitted.X, emitted.Y, emitted.Z,
                    light.Type == ClusteredLightType::Spot ? 1.0f : 0.0f,
                    direction.X, direction.Y, direction.Z,
                    std::cos(light.OuterAngle), std::cos(light.InnerAngle),
                    0.0f, 0.0f, 0.0f,
                };
                for (int f = 0; f < kFloatsPerLight; ++f)
                    texels[static_cast<std::size_t>(index) * kFloatsPerLight +
                           static_cast<std::size_t>(f)] = PackFloat(values[f]);
            }
            lightData_ = std::make_unique<Texture2D>(device_, kFloatsPerLight, rows);
            lightData_->SetData(texels.data(), static_cast<int>(texels.size()));
        }

        // ── The cluster table ────────────────────────────────────────────────
        {
            const int wanted = std::max(2, clusterCount_ * 2);
            const int rows = RowsFor(wanted, kTableWidth);
            std::vector<Color> texels(static_cast<std::size_t>(kTableWidth) * rows,
                                      Color(0, 0, 0, 0));
            const std::vector<int>& offsets = assignment.getOffsets();
            for (int cluster = 0; cluster < clusterCount_; ++cluster)
            {
                const int begin = offsets[static_cast<std::size_t>(cluster)];
                const int end   = offsets[static_cast<std::size_t>(cluster) + 1];
                texels[static_cast<std::size_t>(cluster) * 2] =
                    PackUInt32(static_cast<std::uint32_t>(begin));
                texels[static_cast<std::size_t>(cluster) * 2 + 1] =
                    PackUInt32(static_cast<std::uint32_t>(end - begin));
            }
            clusterTable_ = std::make_unique<Texture2D>(device_, kTableWidth, rows);
            clusterTable_->SetData(texels.data(), static_cast<int>(texels.size()));
        }

        // ── The index list ───────────────────────────────────────────────────
        {
            const int rows = RowsFor(std::max(1, referenceCount_), kTableWidth);
            std::vector<Color> texels(static_cast<std::size_t>(kTableWidth) * rows,
                                      Color(0, 0, 0, 0));
            const std::vector<int>& indices = assignment.getIndices();
            for (int i = 0; i < referenceCount_; ++i)
                texels[static_cast<std::size_t>(i)] =
                    PackUInt32(static_cast<std::uint32_t>(indices[static_cast<std::size_t>(i)]));
            indexList_ = std::make_unique<Texture2D>(device_, kTableWidth, rows);
            indexList_->SetData(texels.data(), static_cast<int>(texels.size()));
        }

        uploaded_ = true;
    }

    void ClusteredLightBuffer::bind(ShaderEffect& effect, const int firstUnit) const
    {
        if (!uploaded_)
            throw std::runtime_error(
                "CNA::Graphics::ClusteredLightBuffer::bind: nothing has been uploaded, so there is "
                "no light list to bind");

        effect.SetUniformInt("uCnaLightData", firstUnit);
        effect.SetTexture(firstUnit, *lightData_);
        effect.SetUniformInt("uCnaClusterTable", firstUnit + 1);
        effect.SetTexture(firstUnit + 1, *clusterTable_);
        effect.SetUniformInt("uCnaLightIndices", firstUnit + 2);
        effect.SetTexture(firstUnit + 2, *indexList_);

        effect.SetUniformInt("uCnaTilesX", tilesX_);
        effect.SetUniformInt("uCnaTilesY", tilesY_);
        effect.SetUniformInt("uCnaSliceCount", sliceCount_);
        effect.SetUniformInt("uCnaLightCount", lightCount_);
        effect.SetUniformFloat("uCnaGridNear", nearPlane_);
        effect.SetUniformFloat("uCnaGridFar", farPlane_);
    }

    std::string ClusteredLightBuffer::getLightLookupGlsl()
    {
        return R"(
uniform sampler2D uCnaLightData;
uniform sampler2D uCnaClusterTable;
uniform sampler2D uCnaLightIndices;
uniform int   uCnaTilesX;
uniform int   uCnaTilesY;
uniform int   uCnaSliceCount;
uniform int   uCnaLightCount;
uniform float uCnaGridNear;
uniform float uCnaGridFar;

const int kCnaFloatsPerLight = 16;
const int kCnaTableWidth     = 256;

struct CnaClusteredLight {
    vec3  position;
    float range;
    vec3  colour;      // the light's colour already multiplied by its intensity
    float isSpot;      // 1.0 for a spot light, 0.0 for a point light
    vec3  direction;
    float cosOuter;
    float cosInner;
};

/// The four bytes of a texel as the 32-bit value they were written from. Exact because the texture
/// is read with texelFetch, which does no filtering and no coordinate rounding.
uint cnaUnpackUint(vec4 texel) {
    return uint(texel.r * 255.0 + 0.5)
         | (uint(texel.g * 255.0 + 0.5) << 8)
         | (uint(texel.b * 255.0 + 0.5) << 16)
         | (uint(texel.a * 255.0 + 0.5) << 24);
}

float cnaUnpackFloat(vec4 texel) { return uintBitsToFloat(cnaUnpackUint(texel)); }

CnaClusteredLight cnaLoadLight(int index) {
    CnaClusteredLight light;
    light.position  = vec3(cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(0, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(1, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(2, index), 0)));
    light.range     = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(3, index), 0));
    light.colour    = vec3(cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(4, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(5, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(6, index), 0)));
    light.isSpot    = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(7, index), 0));
    light.direction = vec3(cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(8, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(9, index), 0)),
                           cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(10, index), 0)));
    light.cosOuter  = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(11, index), 0));
    light.cosInner  = cnaUnpackFloat(texelFetch(uCnaLightData, ivec2(12, index), 0));
    return light;
}

/// The cluster a fragment belongs to. The depth argument is a *view distance* -- positive, in world
/// units -- and not a depth-buffer value, because the slice spacing is a ratio of world distances
/// and converting one to the other is the caller's business, not this table's.
int cnaClusterFromNdc(vec2 ndc, float viewDistance) {
    int tx = clamp(int((ndc.x * 0.5 + 0.5) * float(uCnaTilesX)), 0, uCnaTilesX - 1);
    int ty = clamp(int((ndc.y * 0.5 + 0.5) * float(uCnaTilesY)), 0, uCnaTilesY - 1);
    float span = log(uCnaGridFar / uCnaGridNear);
    float t = log(max(viewDistance, uCnaGridNear) / uCnaGridNear) / max(span, 1e-6);
    int tz = clamp(int(floor(t * float(uCnaSliceCount))), 0, uCnaSliceCount - 1);
    return (tz * uCnaTilesY + ty) * uCnaTilesX + tx;
}

int cnaClusterLightCount(int cluster) {
    int texel = cluster * 2 + 1;
    return int(cnaUnpackUint(texelFetch(uCnaClusterTable,
                                        ivec2(texel % kCnaTableWidth, texel / kCnaTableWidth), 0)));
}

int cnaClusterLightIndex(int cluster, int i) {
    int start = int(cnaUnpackUint(texelFetch(uCnaClusterTable,
                                             ivec2((cluster * 2) % kCnaTableWidth,
                                                   (cluster * 2) / kCnaTableWidth), 0)));
    int texel = start + i;
    return int(cnaUnpackUint(texelFetch(uCnaLightIndices,
                                        ivec2(texel % kCnaTableWidth, texel / kCnaTableWidth), 0)));
}
)";
    }

    int  ClusteredLightBuffer::getLightCount()     const { return lightCount_; }
    int  ClusteredLightBuffer::getClusterCount()   const { return clusterCount_; }
    int  ClusteredLightBuffer::getReferenceCount() const { return referenceCount_; }
    bool ClusteredLightBuffer::isUploaded()        const { return uploaded_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
