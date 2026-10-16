// SPDX-License-Identifier: MS-PL
// The PBR shading basis, guarded as EasyGL guards it (GSC-0009). A singular World leaves a zero
// normal, and a missing, zero or normal-parallel tangent a zero Gram-Schmidt residual; normalize()
// of either is NaN. A direction is divided by its largest component first, which only an exactly
// zero vector fails, so a World scaled by 1e5 is not taken for a singular one. Every select below
// chooses between finite values, so no NaN appears even where vs_3_0 blends both sides.

// normalize() that keeps a zero vector zero. The divisor is the largest component, or 1 for a zero
// vector: no tiny epsilon, whose reciprocal fxc would fold into a constant MojoShader's GLSL output
// cannot print (1e30 overflows its 32-byte number buffer).
float3 CnaPbrDirectionOrZero(float3 v)
{
    float scale = max(max(abs(v.x), abs(v.y)), abs(v.z));
    float3 u = v / (scale + (scale == 0.0 ? 1.0 : 0.0));
    return u * rsqrt(max(dot(u, u), 1.0));
}

// The unit shading normal; +Z where the interpolated normal vanished.
float3 CnaPbrShadingNormal(float3 normal)
{
    float3 n = CnaPbrDirectionOrZero(normal);
    return dot(n, n) > 0.5 ? n : float3(0.0, 0.0, 1.0);
}

// The unit tangent orthogonal to N; a perpendicular direction where the residual is small next to
// the tangent it came from.
float3 CnaPbrShadingTangent(float3 N, float3 tangent)
{
    float3 residual = tangent - N * dot(N, tangent);
    float3 perpendicular = cross(N, abs(N.x) < 0.9 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0));
    return normalize(dot(residual, residual) > 1e-8 * dot(tangent, tangent) ? residual : perpendicular);
}
