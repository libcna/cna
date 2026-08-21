/* SPDX-License-Identifier: MS-PL */
/*
 * FNA3D existence-gate spike (plans/plan_fna3d.md FNA3D-0).
 *
 * Proves, before a single line of CNA renderer code is written, that the FNA3D
 * library actually initializes, clears, presents and reads pixels back in this
 * environment. Every later CNA-side claim rests on this.
 *
 * What it proves:
 *   1. FNA3D_PrepareWindowAttributes() selects a driver and returns the SDL
 *      window flags that driver needs.
 *   2. An SDL3 window created with exactly those flags is accepted by
 *      FNA3D_CreateDevice().
 *   3. FNA3D_Clear() + FNA3D_SwapBuffers() run without error.
 *   4. FNA3D_ReadBackbuffer() returns the exact cleared colour -- a real pixel
 *      oracle, not "the call did not throw".
 *   5. A texture round-trip (CreateTexture2D + SetTextureData2D +
 *      GetTextureData2D) preserves bytes.
 *   6. FNA3D_CreateEffect() accepts a real D3D9 Effect Framework binary
 *      (FNA's stock SpriteEffect.fxb) and MojoShader reports its techniques
 *      and parameters -- the gate for any shaded draw at all, since FNA3D has
 *      no other shader entry point.
 *
 * Build/run: see README.md in this directory.
 */

/* FNA3D.h itself says "When using this API, be sure to include mojoshader.h!" -- without it
 * MOJOSHADER_effect stays an incomplete type and the Effect Framework structs are unreachable.
 * MOJOSHADER_EFFECT_SUPPORT is the same define FNA3D's own build sets. */
#define MOJOSHADER_EFFECT_SUPPORT 1
#include <mojoshader.h>

#include <FNA3D.h>
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
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

int main(int argc, char **argv)
{
	SDL_Window *window;
	FNA3D_PresentationParameters params;
	FNA3D_Device *device;
	FNA3D_Vec4 clearColor;
	FNA3D_Texture *texture;
	uint32_t flags;
	uint8_t *pixels;
	uint8_t source[4 * 4 * 4];
	uint8_t readback[4 * 4 * 4];
	const int width = 64;
	const int height = 48;
	int i;

	(void) argc;
	(void) argv;

	FNA3D_HookLogFunctions(logInfo, logWarn, logError);

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	flags = FNA3D_PrepareWindowAttributes();
	printf("[info] FNA3D_PrepareWindowAttributes -> 0x%08x\n", (unsigned) flags);
	check(flags != 0 || SDL_GetHint("FNA3D_FORCE_DRIVER") != NULL,
	      "FNA3D_PrepareWindowAttributes returned window flags");

	window = SDL_CreateWindow(
		"CNA FNA3D spike",
		width,
		height,
		(SDL_WindowFlags) flags | SDL_WINDOW_HIDDEN
	);
	if (window == NULL)
	{
		printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_memset(&params, '\0', sizeof(params));
	params.backBufferWidth = width;
	params.backBufferHeight = height;
	params.backBufferFormat = FNA3D_SURFACEFORMAT_COLOR;
	params.multiSampleCount = 0;
	params.deviceWindowHandle = window;
	params.isFullScreen = 0;
	params.depthStencilFormat = FNA3D_DEPTHFORMAT_D24S8;
	params.presentationInterval = FNA3D_PRESENTINTERVAL_IMMEDIATE;
	params.displayOrientation = FNA3D_DISPLAYORIENTATION_DEFAULT;
	params.renderTargetUsage = FNA3D_RENDERTARGETUSAGE_DISCARDCONTENTS;

	device = FNA3D_CreateDevice(&params, 1);
	check(device != NULL, "FNA3D_CreateDevice");
	if (device == NULL)
	{
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	/* 2. Clear to an exact colour and read it back. */
	clearColor.x = 0.0f;
	clearColor.y = 1.0f;
	clearColor.z = 0.0f;
	clearColor.w = 1.0f;
	FNA3D_Clear(
		device,
		(FNA3D_ClearOptions) (
			FNA3D_CLEAROPTIONS_TARGET |
			FNA3D_CLEAROPTIONS_DEPTHBUFFER |
			FNA3D_CLEAROPTIONS_STENCIL
		),
		&clearColor,
		1.0f,
		0
	);
	FNA3D_SwapBuffers(device, NULL, NULL, window);

	pixels = (uint8_t*) SDL_malloc((size_t) width * height * 4);
	FNA3D_ReadBackbuffer(device, 0, 0, width, height, pixels, width * height * 4);
	check(pixels[0] == 0 && pixels[1] == 255 && pixels[2] == 0 && pixels[3] == 255,
	      "ReadBackbuffer sees the exact cleared colour at (0,0)");
	printf("[info] backbuffer(0,0) = %u,%u,%u,%u\n",
	       pixels[0], pixels[1], pixels[2], pixels[3]);
	{
		const size_t last = ((size_t) height - 1) * width * 4 + ((size_t) width - 1) * 4;
		check(pixels[last + 0] == 0 && pixels[last + 1] == 255 &&
		      pixels[last + 2] == 0 && pixels[last + 3] == 255,
		      "ReadBackbuffer sees the exact cleared colour at the far corner");
	}
	SDL_free(pixels);

	/* 3. Texture round-trip. */
	for (i = 0; i < (int) sizeof(source); i += 1)
	{
		source[i] = (uint8_t) (i * 7 + 3);
	}
	texture = FNA3D_CreateTexture2D(
		device, FNA3D_SURFACEFORMAT_COLOR, 4, 4, 1, 0
	);
	check(texture != NULL, "FNA3D_CreateTexture2D");
	if (texture != NULL)
	{
		FNA3D_SetTextureData2D(
			device, texture, 0, 0, 4, 4, 0, source, (int32_t) sizeof(source)
		);
		SDL_memset(readback, '\0', sizeof(readback));
		FNA3D_GetTextureData2D(
			device, texture, 0, 0, 4, 4, 0, readback, (int32_t) sizeof(readback)
		);
		check(SDL_memcmp(source, readback, sizeof(source)) == 0,
		      "Texture2D SetData/GetData round-trip preserves bytes");
		FNA3D_AddDisposeTexture(device, texture);
	}

	/* 4. Real D3D9 Effect Framework binary. */
	{
		const char *fxbPath = SDL_getenv("FNA3D_SPIKE_FXB");
		if (fxbPath == NULL)
		{
			printf("[SKIP] no FNA3D_SPIKE_FXB set, effect gate not exercised\n");
		}
		else
		{
			size_t blobSize = 0;
			void *blob = SDL_LoadFile(fxbPath, &blobSize);
			check(blob != NULL && blobSize > 0, "loaded the .fxb blob");
			if (blob != NULL)
			{
				FNA3D_Effect *effect = NULL;
				MOJOSHADER_effect *effectData = NULL;
				FNA3D_CreateEffect(
					device, (uint8_t*) blob, (uint32_t) blobSize,
					&effect, &effectData
				);
				check(effect != NULL && effectData != NULL,
				      "FNA3D_CreateEffect parsed the Effect binary");
				if (effectData != NULL)
				{
					printf("[info] effect: %d techniques, %d params, %d errors\n",
					       effectData->technique_count,
					       effectData->param_count,
					       effectData->error_count);
					for (i = 0; i < effectData->param_count; i += 1)
					{
						printf("[info]   param[%d] = \"%s\" (%u values)\n",
						       i,
						       effectData->params[i].value.name,
						       effectData->params[i].value.value_count);
					}
					for (i = 0; i < effectData->technique_count; i += 1)
					{
						printf("[info]   technique[%d] = \"%s\" (%u passes)\n",
						       i,
						       effectData->techniques[i].name,
						       effectData->techniques[i].pass_count);
					}
					check(effectData->error_count == 0,
					      "Effect parsed with no MojoShader errors");
					check(effectData->technique_count > 0,
					      "Effect exposes at least one technique");
				}
				if (effect != NULL)
				{
					FNA3D_AddDisposeEffect(device, effect);
				}
				SDL_free(blob);
			}
		}
	}

	FNA3D_DestroyDevice(device);
	SDL_DestroyWindow(window);
	SDL_Quit();

	printf("%s (%d failure(s))\n", failures == 0 ? "SPIKE OK" : "SPIKE FAILED", failures);
	return failures == 0 ? 0 : 1;
}
