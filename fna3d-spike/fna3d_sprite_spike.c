/* SPDX-License-Identifier: MS-PL */
/*
 * FNA3D textured-quad spike (plans/plan_fna3d.md FNA3D-0b).
 *
 * The device spike proved clear/present/readback/texture/effect-parsing. This one proves the
 * remaining half of the vertical slice with no CNA code in the picture at all: a real draw call
 * through FNA's stock SpriteEffect, verified by reading the exact rendered pixel back.
 *
 * It exists because the first CNA SpriteBatch implementation rendered nothing, and the only way
 * to tell "CNA is driving FNA3D wrongly" apart from "FNA3D needs something else here" is to
 * reproduce the call sequence standalone.
 *
 * Build/run: see README.md.
 */

#define MOJOSHADER_EFFECT_SUPPORT 1
#include <mojoshader.h>

#include <FNA3D.h>
#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *label)
{
	printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", label);
	if (!condition)
	{
		failures += 1;
	}
}

static void logInfo(const char *msg)  { printf("[FNA3D:info] %s\n", msg); }
static void logWarn(const char *msg)  { printf("[FNA3D:warn] %s\n", msg); }
static void logError(const char *msg) { printf("[FNA3D:error] %s\n", msg); }

typedef struct SpriteVertex
{
	float x, y, z;
	uint32_t color;
	float u, v;
} SpriteVertex;

int main(int argc, char **argv)
{
	SDL_Window *window;
	FNA3D_PresentationParameters params;
	FNA3D_Device *device;
	FNA3D_Vec4 clearColor;
	FNA3D_Texture *texture;
	FNA3D_Buffer *vertexBuffer;
	FNA3D_Buffer *indexBuffer;
	FNA3D_Effect *effect = NULL;
	MOJOSHADER_effect *effectData = NULL;
	MOJOSHADER_effectStateChanges stateChanges;
	MOJOSHADER_effectParam *matrixParam = NULL;
	FNA3D_BlendState blend;
	FNA3D_DepthStencilState depthStencil;
	FNA3D_RasterizerState rasterizer;
	FNA3D_SamplerState sampler;
	FNA3D_Viewport viewport;
	FNA3D_VertexElement elements[3];
	FNA3D_VertexBufferBinding binding;
	SpriteVertex quad[4];
	uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
	uint8_t white[4] = { 255, 255, 255, 255 };
	uint8_t pixel[4];
	const int width = 128;
	const int height = 96;
	const char *fxbPath = SDL_getenv("FNA3D_SPIKE_FXB");
	void *blob;
	size_t blobSize = 0;
	float *matrix;
	int i;

	(void) argc;
	(void) argv;

	if (fxbPath == NULL)
	{
		printf("[FAIL] set FNA3D_SPIKE_FXB to SpriteEffect.fxb\n");
		return 1;
	}

	FNA3D_HookLogFunctions(logInfo, logWarn, logError);
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	window = SDL_CreateWindow(
		"CNA FNA3D sprite spike", width, height,
		(SDL_WindowFlags) FNA3D_PrepareWindowAttributes() | SDL_WINDOW_HIDDEN
	);
	if (window == NULL)
	{
		printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
		return 1;
	}

	SDL_memset(&params, '\0', sizeof(params));
	params.backBufferWidth = width;
	params.backBufferHeight = height;
	params.backBufferFormat = FNA3D_SURFACEFORMAT_COLOR;
	params.deviceWindowHandle = window;
	params.depthStencilFormat = FNA3D_DEPTHFORMAT_D24S8;
	params.presentationInterval = FNA3D_PRESENTINTERVAL_IMMEDIATE;
	params.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;
	device = FNA3D_CreateDevice(&params, 1);
	check(device != NULL, "FNA3D_CreateDevice");
	if (device == NULL)
	{
		return 1;
	}

	blob = SDL_LoadFile(fxbPath, &blobSize);
	FNA3D_CreateEffect(device, (uint8_t*) blob, (uint32_t) blobSize, &effect, &effectData);
	check(effect != NULL && effectData != NULL, "FNA3D_CreateEffect(SpriteEffect)");
	for (i = 0; i < effectData->param_count; i += 1)
	{
		if (SDL_strcmp(effectData->params[i].value.name, "MatrixTransform") == 0)
		{
			matrixParam = &effectData->params[i];
		}
	}
	check(matrixParam != NULL, "SpriteEffect declares MatrixTransform");

	/* One white texel. */
	texture = FNA3D_CreateTexture2D(device, FNA3D_SURFACEFORMAT_COLOR, 1, 1, 1, 0);
	FNA3D_SetTextureData2D(device, texture, 0, 0, 1, 1, 0, white, 4);

	/* A 32x24 quad at (16,12), white, in pixel coordinates. */
	SDL_memset(quad, '\0', sizeof(quad));
	quad[0].x = 16.0f;  quad[0].y = 12.0f;  quad[0].u = 0.0f; quad[0].v = 0.0f;
	quad[1].x = 48.0f;  quad[1].y = 12.0f;  quad[1].u = 1.0f; quad[1].v = 0.0f;
	quad[2].x = 48.0f;  quad[2].y = 36.0f;  quad[2].u = 1.0f; quad[2].v = 1.0f;
	quad[3].x = 16.0f;  quad[3].y = 36.0f;  quad[3].u = 0.0f; quad[3].v = 1.0f;
	for (i = 0; i < 4; i += 1)
	{
		quad[i].color = 0xFFFFFFFFu;
	}

	vertexBuffer = FNA3D_GenVertexBuffer(device, 1, FNA3D_BUFFERUSAGE_WRITEONLY, sizeof(quad));
	FNA3D_SetVertexBufferData(device, vertexBuffer, 0, quad, 4, sizeof(SpriteVertex),
	                          sizeof(SpriteVertex), FNA3D_SETDATAOPTIONS_DISCARD);
	indexBuffer = FNA3D_GenIndexBuffer(device, 1, FNA3D_BUFFERUSAGE_WRITEONLY, sizeof(indices));
	FNA3D_SetIndexBufferData(device, indexBuffer, 0, indices, sizeof(indices),
	                         FNA3D_SETDATAOPTIONS_DISCARD);

	/* Opaque blend, no depth, no culling -- the same state CNA's sprite path uses. */
	SDL_memset(&blend, '\0', sizeof(blend));
	blend.colorSourceBlend = FNA3D_BLEND_ONE;
	blend.colorDestinationBlend = FNA3D_BLEND_ZERO;
	blend.colorBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	blend.alphaSourceBlend = FNA3D_BLEND_ONE;
	blend.alphaDestinationBlend = FNA3D_BLEND_ZERO;
	blend.alphaBlendFunction = FNA3D_BLENDFUNCTION_ADD;
	blend.colorWriteEnable = FNA3D_COLORWRITECHANNELS_ALL;
	blend.colorWriteEnable1 = FNA3D_COLORWRITECHANNELS_ALL;
	blend.colorWriteEnable2 = FNA3D_COLORWRITECHANNELS_ALL;
	blend.colorWriteEnable3 = FNA3D_COLORWRITECHANNELS_ALL;
	blend.multiSampleMask = -1;
	FNA3D_SetBlendState(device, &blend);

	SDL_memset(&depthStencil, '\0', sizeof(depthStencil));
	depthStencil.depthBufferFunction = FNA3D_COMPAREFUNCTION_LESSEQUAL;
	depthStencil.stencilMask = -1;
	depthStencil.stencilWriteMask = -1;
	depthStencil.stencilFunction = FNA3D_COMPAREFUNCTION_ALWAYS;
	depthStencil.ccwStencilFunction = FNA3D_COMPAREFUNCTION_ALWAYS;
	FNA3D_SetDepthStencilState(device, &depthStencil);

	SDL_memset(&rasterizer, '\0', sizeof(rasterizer));
	rasterizer.fillMode = FNA3D_FILLMODE_SOLID;
	rasterizer.cullMode = FNA3D_CULLMODE_NONE;
	rasterizer.multiSampleAntiAlias = 1;
	FNA3D_ApplyRasterizerState(device, &rasterizer);

	viewport.x = 0;
	viewport.y = 0;
	viewport.w = width;
	viewport.h = height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	FNA3D_SetViewport(device, &viewport);

	clearColor.x = 0.0f;
	clearColor.y = 0.0f;
	clearColor.z = 0.0f;
	clearColor.w = 1.0f;
	FNA3D_Clear(device, FNA3D_CLEAROPTIONS_TARGET | FNA3D_CLEAROPTIONS_DEPTHBUFFER,
	            &clearColor, 1.0f, 0);

	/* XNA's SpriteBatch projection, packed column-first the way FNA's EffectParameter does. */
	matrix = matrixParam->value.valuesF;
	SDL_memset(matrix, '\0', 16 * sizeof(float));
	matrix[0] = 2.0f / (float) width;     /* M11 */
	matrix[5] = -2.0f / (float) height;   /* M22 */
	matrix[10] = 1.0f;                    /* M33 */
	matrix[3] = -1.0f;                    /* M41 -> register 0, component 3 */
	matrix[7] = 1.0f;                     /* M42 -> register 1, component 3 */
	matrix[15] = 1.0f;                    /* M44 */

	SDL_memset(&stateChanges, '\0', sizeof(stateChanges));
	FNA3D_SetEffectTechnique(device, effect, &effectData->techniques[0]);
	FNA3D_ApplyEffect(device, effect, 0, &stateChanges);

	SDL_memset(&sampler, '\0', sizeof(sampler));
	sampler.filter = FNA3D_TEXTUREFILTER_POINT;
	sampler.addressU = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampler.addressV = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampler.addressW = FNA3D_TEXTUREADDRESSMODE_CLAMP;
	sampler.maxAnisotropy = 1;
	FNA3D_VerifySampler(device, 0, texture, &sampler);

	elements[0].offset = 0;
	elements[0].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR3;
	elements[0].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_POSITION;
	elements[0].usageIndex = 0;
	elements[1].offset = 12;
	elements[1].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_COLOR;
	elements[1].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_COLOR;
	elements[1].usageIndex = 0;
	elements[2].offset = 16;
	elements[2].vertexElementFormat = FNA3D_VERTEXELEMENTFORMAT_VECTOR2;
	elements[2].vertexElementUsage = FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE;
	elements[2].usageIndex = 0;

	SDL_memset(&binding, '\0', sizeof(binding));
	binding.vertexBuffer = vertexBuffer;
	binding.vertexDeclaration.vertexStride = sizeof(SpriteVertex);
	binding.vertexDeclaration.elementCount = 3;
	binding.vertexDeclaration.elements = elements;
	binding.vertexOffset = 0;
	binding.instanceFrequency = 0;
	FNA3D_ApplyVertexBufferBindings(device, &binding, 1, 1, 0);

	FNA3D_DrawIndexedPrimitives(device, FNA3D_PRIMITIVETYPE_TRIANGLELIST, 0, 0, 4, 0, 2,
	                            indexBuffer, FNA3D_INDEXELEMENTSIZE_16BIT);

	/* The quad's destination rectangle in XNA pixel coordinates is (16,12)-(48,36). A FULL-FRAME
	 * read is top-row-first on every FNA3D driver, so this locates the rendered quad exactly. */
	{
		uint8_t *frame = SDL_malloc((size_t) width * height * 4);
		int px, py, minx = 9999, miny = 9999, maxx = -1, maxy = -1, count = 0;
		FNA3D_ReadBackbuffer(device, 0, 0, width, height, frame, width * height * 4);
		for (py = 0; py < height; py += 1)
		for (px = 0; px < width; px += 1)
		{
			if (frame[(py * width + px) * 4] > 128)
			{
				count += 1;
				if (px < minx) minx = px;
				if (px > maxx) maxx = px;
				if (py < miny) miny = py;
				if (py > maxy) maxy = py;
			}
		}
		printf("[info] white pixels=%d bbox=(%d,%d)-(%d,%d)\n", count, minx, miny, maxx, maxy);
		check(count == 32 * 24, "the textured quad covers exactly its 32x24 destination");
		check(minx == 16 && miny == 12 && maxx == 47 && maxy == 35,
		      "the quad lands at the XNA pixel rectangle it was given, top-row-first");
		SDL_free(frame);
	}

	/* THE FINDING that shaped CNA's ReadBackbuffer: the SUB-RECTANGLE form of the same call is
	 * NOT top-left on the OpenGL driver. It forwards (x, y) straight to glReadPixels, whose
	 * origin is the bottom-left, and only flips the returned rows afterwards -- so this read of
	 * the rectangle the quad demonstrably occupies comes back as the cleared colour, while the
	 * mirrored row does contain it. The D3D11 driver, routing through GetTextureData2D, uses the
	 * top-left origin instead. CNA's Fna3dRenderer therefore reads the whole backbuffer and crops
	 * in CNA rather than forwarding the rectangle. */
	FNA3D_ReadBackbuffer(device, 32, 24, 1, 1, pixel, 4);
	printf("[info] sub-rect read at top-left (32,24)    = %u,%u,%u,%u\n",
	       pixel[0], pixel[1], pixel[2], pixel[3]);
	check(pixel[0] == 0,
	      "sub-rectangle reads are NOT top-left-origin on the OpenGL driver (documented quirk)");

	FNA3D_ReadBackbuffer(device, 32, height - 24 - 1, 1, 1, pixel, 4);
	printf("[info] sub-rect read at bottom-origin y     = %u,%u,%u,%u\n",
	       pixel[0], pixel[1], pixel[2], pixel[3]);
	check(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255,
	      "the same sub-rectangle read with a bottom-left origin does find the quad");

	FNA3D_AddDisposeEffect(device, effect);
	FNA3D_AddDisposeIndexBuffer(device, indexBuffer);
	FNA3D_AddDisposeVertexBuffer(device, vertexBuffer);
	FNA3D_AddDisposeTexture(device, texture);
	FNA3D_DestroyDevice(device);
	SDL_free(blob);
	SDL_DestroyWindow(window);
	SDL_Quit();

	printf("%s (%d failure(s))\n", failures == 0 ? "SPIKE OK" : "SPIKE FAILED", failures);
	return failures == 0 ? 0 : 1;
}
