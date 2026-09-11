/* SPDX-License-Identifier: MS-PL
 *
 * plans/plan_xna_sample_xnb_sweep.md XNASWEEP-149: the second Microsoft black box.
 *
 * `MeshHelper.OptimizeForCache` is measured by OptimizeForCacheOracle.cs; this driver hands the
 * *same* probe file, built into the *same* index buffer, to the documented public D3DX9 mesh
 * optimisation entry points, so the two answers can be compared face for face.
 *
 * Nothing here reads, disassembles or decompiles Microsoft's implementation.  The DLL is loaded by
 * its documented name, the functions are found by their documented exported names, and the only
 * thing observed is what they return.  No Microsoft binary is copied into this repository and CNA
 * gains no runtime dependency on any of it.
 *
 * Documented signatures (public Direct3D 9 / D3DX documentation):
 *
 *   HRESULT WINAPI D3DXOptimizeFaces   (LPCVOID pbIndices, UINT cFaces, UINT cVertices,
 *                                       BOOL b32BitIndices, DWORD *pFaceRemap);
 *   HRESULT WINAPI D3DXOptimizeVertices(LPCVOID pbIndices, UINT cFaces, UINT cVertices,
 *                                       BOOL b32BitIndices, DWORD *pVertexRemap);
 *
 * Input is the probe text file generate.py writes:
 *
 *   P <name> <shared:0|1> <positionCount> <faceCount>
 *   V <x> <y> <z>
 *   F <a> <b> <c>
 *
 * and one line per probe comes back:
 *
 *   <name>|<faceRemap>|<vertexRemap>
 *
 * both remaps verbatim, as DWORD lists, uninterpreted.  Which way round a remap reads is a
 * question for the analysis, not for the transcriber.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef HRESULT (WINAPI *PFN_OPTIMIZE)(LPCVOID, UINT, UINT, BOOL, DWORD *);

#define MAX_POS   8192
#define MAX_FACE  16384

static int   g_face[MAX_FACE][3];
static DWORD g_indices[MAX_FACE * 3];
static int   g_vertexPosition[MAX_FACE * 3];
static DWORD g_faceRemap[MAX_FACE];
static DWORD g_vertexRemap[MAX_FACE * 3];
static int   g_vertexOfPosition[MAX_POS];

int main(int argc, char **argv)
{
    const char *dllPath;
    HMODULE lib;
    PFN_OPTIMIZE optimizeFaces, optimizeVertices;
    FILE *in, *out;
    char line[4096];
    char loaded[MAX_PATH];

    if (argc < 4) {
        fprintf(stderr, "usage: D3dxOptimizeOracle <d3dx9.dll> <probes.txt> <out.txt>\n");
        return 2;
    }
    dllPath = argv[1];

    lib = LoadLibraryA(dllPath);
    if (!lib) {
        fprintf(stderr, "D3dxOptimizeOracle: LoadLibraryA(%s) failed, error %lu\n",
                dllPath, (unsigned long)GetLastError());
        return 3;
    }
    if (GetModuleFileNameA(lib, loaded, sizeof(loaded)))
        fprintf(stderr, "D3dxOptimizeOracle: loaded %s\n", loaded);

    optimizeFaces    = (PFN_OPTIMIZE)GetProcAddress(lib, "D3DXOptimizeFaces");
    optimizeVertices = (PFN_OPTIMIZE)GetProcAddress(lib, "D3DXOptimizeVertices");
    if (!optimizeFaces) {
        fprintf(stderr, "D3dxOptimizeOracle: no D3DXOptimizeFaces export\n");
        return 3;
    }
    if (!optimizeVertices)
        fprintf(stderr, "D3dxOptimizeOracle: no D3DXOptimizeVertices export; face remap only\n");

    in = fopen(argv[2], "rb");
    if (!in) { fprintf(stderr, "D3dxOptimizeOracle: cannot read %s\n", argv[2]); return 3; }
    out = fopen(argv[3], "wb");
    if (!out) { fprintf(stderr, "D3dxOptimizeOracle: cannot write %s\n", argv[3]); return 3; }

    while (fgets(line, sizeof(line), in)) {
        char name[512];
        int shared = 0, positionCount = 0, faceCount = 0;
        int i, c, vertexCount = 0;
        HRESULT hrFaces, hrVertices = S_OK;

        if (line[0] != 'P')
            continue;
        if (sscanf(line, "P %511s %d %d %d", name, &shared, &positionCount, &faceCount) != 4)
            continue;
        if (positionCount > MAX_POS || faceCount > MAX_FACE) {
            fprintf(out, "%s|TOOBIG|TOOBIG\n", name);
            continue;
        }
        for (i = 0; i < positionCount; i++)
            if (!fgets(line, sizeof(line), in)) break;          /* V lines: positions unused here */
        for (i = 0; i < faceCount; i++) {
            if (!fgets(line, sizeof(line), in)) break;
            if (sscanf(line, "F %d %d %d", &g_face[i][0], &g_face[i][1], &g_face[i][2]) != 3)
                break;
        }

        /* Build the same vertex list and index buffer OptimizeForCacheOracle.cs builds: shared
         * meshes give one vertex per distinct position in first-encounter order, unshared meshes
         * one vertex per corner. */
        for (i = 0; i < positionCount; i++)
            g_vertexOfPosition[i] = -1;
        for (i = 0; i < faceCount; i++) {
            for (c = 0; c < 3; c++) {
                int position = g_face[i][c], vertex;
                if (shared) {
                    if (g_vertexOfPosition[position] < 0) {
                        g_vertexOfPosition[position] = vertexCount;
                        g_vertexPosition[vertexCount] = position;
                        vertexCount++;
                    }
                    vertex = g_vertexOfPosition[position];
                } else {
                    vertex = vertexCount;
                    g_vertexPosition[vertexCount] = position;
                    vertexCount++;
                }
                g_indices[i * 3 + c] = (DWORD)vertex;
            }
        }

        memset(g_faceRemap, 0xFF, sizeof(DWORD) * (size_t)faceCount);
        memset(g_vertexRemap, 0xFF, sizeof(DWORD) * (size_t)vertexCount);

        hrFaces = optimizeFaces(g_indices, (UINT)faceCount, (UINT)vertexCount, TRUE, g_faceRemap);
        if (optimizeVertices)
            hrVertices = optimizeVertices(g_indices, (UINT)faceCount, (UINT)vertexCount, TRUE,
                                          g_vertexRemap);

        if (FAILED(hrFaces)) {
            fprintf(out, "%s|ERROR 0x%08lx|\n", name, (unsigned long)hrFaces);
            continue;
        }
        fprintf(out, "%s|", name);
        for (i = 0; i < faceCount; i++)
            fprintf(out, "%s%lu", i ? "," : "", (unsigned long)g_faceRemap[i]);
        fprintf(out, "|");
        if (optimizeVertices && SUCCEEDED(hrVertices))
            for (i = 0; i < vertexCount; i++)
                fprintf(out, "%s%lu", i ? "," : "", (unsigned long)g_vertexRemap[i]);
        fprintf(out, "\n");
    }
    fclose(in);
    fclose(out);
    return 0;
}
