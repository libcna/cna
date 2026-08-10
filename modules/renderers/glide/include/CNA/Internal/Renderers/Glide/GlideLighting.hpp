#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace CNA::Internal::Renderers::Glide
{
    /** A small CPU-side vector used to reproduce the BasicEffect fixed-function lighting subset. */
    struct GlideLightingVector
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /** Directional-light data consumed by the CPU BasicEffect implementation. */
    struct GlideDirectionalLight
    {
        GlideLightingVector direction;
        GlideLightingVector diffuse;
        GlideLightingVector specular;
    };

    /** Material and light inputs shared by all vertices in one BasicEffect draw. */
    struct GlideBasicEffectLightingState
    {
        GlideLightingVector ambient;
        GlideLightingVector eyePosition;
        GlideLightingVector materialSpecular;
        std::array<GlideDirectionalLight, 3> lights{};
        float specularPower = 16.0f;
    };

    /** Diffuse illumination and additive specular contribution for one vertex. */
    struct GlideBasicEffectLightingResult
    {
        GlideLightingVector diffuse;
        GlideLightingVector specular;
    };

    [[nodiscard]] inline GlideLightingVector AddGlideLightingVectors(
        const GlideLightingVector& left, const GlideLightingVector& right)
    {
        return {left.x + right.x, left.y + right.y, left.z + right.z};
    }

    [[nodiscard]] inline GlideLightingVector MultiplyGlideLightingVectors(
        const GlideLightingVector& left, const GlideLightingVector& right)
    {
        return {left.x * right.x, left.y * right.y, left.z * right.z};
    }

    [[nodiscard]] inline GlideLightingVector MultiplyGlideLightingVector(
        const GlideLightingVector& vector, float scalar)
    {
        return {vector.x * scalar, vector.y * scalar, vector.z * scalar};
    }

    [[nodiscard]] inline float DotGlideLightingVectors(
        const GlideLightingVector& left, const GlideLightingVector& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    [[nodiscard]] inline GlideLightingVector NormalizeGlideLightingVectorOrZero(
        const GlideLightingVector& vector)
    {
        const float lengthSquared = DotGlideLightingVectors(vector, vector);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000000000001f)
        {
            return {};
        }
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        return MultiplyGlideLightingVector(vector, inverseLength);
    }

    /**
     * Returns the row-major inverse of an affine world's upper-left 3x3 matrix.
     *
     * Row-vector normals multiply the transpose of this result, represented by the multiplication
     * performed in TransformGlideLightingNormal().
     */
    [[nodiscard]] inline std::array<float, 9> InvertGlideLightingWorld3x3(const std::array<float, 9>& world)
    {
        const float determinant =
            world[0] * (world[4] * world[8] - world[5] * world[7]) -
            world[1] * (world[3] * world[8] - world[5] * world[6]) +
            world[2] * (world[3] * world[7] - world[4] * world[6]);
        if (!std::isfinite(determinant) || std::abs(determinant) <= 0.000001f)
        {
            throw std::runtime_error(
                "GLIDE vertex-lit BasicEffect cannot transform normals through a singular World matrix");
        }
        const float inverseDeterminant = 1.0f / determinant;
        return {
            (world[4] * world[8] - world[5] * world[7]) * inverseDeterminant,
            (world[2] * world[7] - world[1] * world[8]) * inverseDeterminant,
            (world[1] * world[5] - world[2] * world[4]) * inverseDeterminant,
            (world[5] * world[6] - world[3] * world[8]) * inverseDeterminant,
            (world[0] * world[8] - world[2] * world[6]) * inverseDeterminant,
            (world[2] * world[3] - world[0] * world[5]) * inverseDeterminant,
            (world[3] * world[7] - world[4] * world[6]) * inverseDeterminant,
            (world[1] * world[6] - world[0] * world[7]) * inverseDeterminant,
            (world[0] * world[4] - world[1] * world[3]) * inverseDeterminant};
    }

    [[nodiscard]] inline GlideLightingVector TransformGlideLightingNormal(
        const GlideLightingVector& normal, const std::array<float, 9>& inverseWorld)
    {
        // Row-vector normals transform as n' = n * inverse(World)^T. Dotting `normal` against
        // each *row* of `inverseWorld` (rather than each column) applies that transpose directly,
        // without needing a separately materialized transposed matrix.
        return NormalizeGlideLightingVectorOrZero({
            normal.x * inverseWorld[0] + normal.y * inverseWorld[1] + normal.z * inverseWorld[2],
            normal.x * inverseWorld[3] + normal.y * inverseWorld[4] + normal.z * inverseWorld[5],
            normal.x * inverseWorld[6] + normal.y * inverseWorld[7] + normal.z * inverseWorld[8]});
    }

    /**
     * Evaluates FNA/XNA BasicEffect's per-vertex directional Blinn-Phong terms before material
     * diffuse and alpha are applied. A light with a back-facing normal contribution also gates
     * its specular term, matching the stock effect shader's `zeroL` multiplication.
     */
    [[nodiscard]] inline GlideBasicEffectLightingResult EvaluateGlideBasicEffectLighting(
        const GlideBasicEffectLightingState& state, const GlideLightingVector& worldPosition,
        const GlideLightingVector& worldNormal)
    {
        GlideBasicEffectLightingResult result{state.ambient, {}};
        const GlideLightingVector eye = NormalizeGlideLightingVectorOrZero({
            state.eyePosition.x - worldPosition.x,
            state.eyePosition.y - worldPosition.y,
            state.eyePosition.z - worldPosition.z});
        for (const GlideDirectionalLight& light : state.lights)
        {
            const float dotLight = -DotGlideLightingVectors(light.direction, worldNormal);
            const float zeroLight = dotLight >= 0.0f ? 1.0f : 0.0f;
            result.diffuse = AddGlideLightingVectors(
                result.diffuse, MultiplyGlideLightingVector(light.diffuse, zeroLight * dotLight));

            const GlideLightingVector halfVector = NormalizeGlideLightingVectorOrZero({
                eye.x - light.direction.x, eye.y - light.direction.y, eye.z - light.direction.z});
            const float halfDot = std::max(DotGlideLightingVectors(halfVector, worldNormal), 0.0f) * zeroLight;
            const float amount = std::pow(halfDot, state.specularPower);
            result.specular = AddGlideLightingVectors(result.specular, MultiplyGlideLightingVectors(
                MultiplyGlideLightingVector(light.specular, amount), state.materialSpecular));
        }
        return result;
    }

    /** Applies BasicEffect's material diffuse/emissive/specular order before fog. */
    [[nodiscard]] inline GlideLightingVector ComposeGlideBasicEffectLitColor(
        const GlideLightingVector& diffuseVertexColor, const GlideBasicEffectLightingResult& lighting,
        const GlideLightingVector& emissive, float alpha)
    {
        return AddGlideLightingVectors(
            AddGlideLightingVectors(MultiplyGlideLightingVectors(diffuseVertexColor, lighting.diffuse), emissive),
            MultiplyGlideLightingVector(lighting.specular, alpha));
    }

    /** Applies FNA BasicEffect fog after the lit specular term, preserving the output alpha. */
    [[nodiscard]] inline GlideLightingVector ApplyGlideBasicEffectFog(
        const GlideLightingVector& color, float alpha, const GlideLightingVector& fogColor, float fogAmount)
    {
        return AddGlideLightingVectors(MultiplyGlideLightingVector(color, 1.0f - fogAmount),
            MultiplyGlideLightingVector(fogColor, alpha * fogAmount));
    }
} // namespace CNA::Internal::Renderers::Glide
