// SPDX-License-Identifier: Zlib
// Altered source: mono DSP extraction, C++ namespace/standard-library adaptation for CNA.
/* FAudio - XAudio Reimplementation for FNA
 *
 * Copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma, and the MonoGame Team
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace CNA::ThirdParty::FAudioReverb {
using FAudioMallocFunc = void* (*)(size_t);
using FAudioFreeFunc = void (*)(void*);
constexpr float FAUDIOFX_REVERB_MIN_DECAY_TIME = 0.1f;
struct FAudioFXReverbParameters {
 float WetDryMix;
 uint32_t ReflectionsDelay;
 uint8_t ReverbDelay, RearDelay, PositionLeft, PositionRight, PositionMatrixLeft, PositionMatrixRight;
 uint8_t EarlyDiffusion, LateDiffusion, LowEQGain, LowEQCutoff, HighEQGain, HighEQCutoff;
 float RoomFilterFreq, RoomFilterMain, RoomFilterHF, ReflectionsGain, ReverbGain, DecayTime, Density, RoomSize;
};
static inline float DbGainToFactor(float gain)
{
	return (float) std::pow(10, gain / 20.0f);
}

static inline uint32_t MsToSamples(float msec, int32_t sampleRate)
{
	return (uint32_t) ((sampleRate * msec) / 1000.0f);
}

#ifndef DISABLE_SUBNORMALS
#define Undenormalize(a) ((a))
#else /* DISABLE_SUBNORMALS */
static inline float Undenormalize(float sample_in)
{
	if (IS_SUBNORMAL(sample_in))
	{
		return 0.0f;
	}
	return sample_in;
}
#endif /* DISABLE_SUBNORMALS */

/* Component - Delay */

#define DSP_DELAY_MAX_DELAY_MS 300

typedef struct DspDelay
{
	int32_t	 sampleRate;
	uint32_t capacity;	/* In samples */
	uint32_t delay;		/* In samples */
	uint32_t read_idx;
	uint32_t write_idx;
	float *buffer;
} DspDelay;

static inline void DspDelay_Initialize(
	DspDelay *filter,
	int32_t sampleRate,
	float delay_ms,
	FAudioMallocFunc pMalloc
) {
	assert(delay_ms >= 0 && delay_ms <= DSP_DELAY_MAX_DELAY_MS);

	filter->sampleRate = sampleRate;
	filter->capacity = MsToSamples(DSP_DELAY_MAX_DELAY_MS, sampleRate);
	filter->delay = MsToSamples(delay_ms, sampleRate);
	filter->read_idx = 0;
	filter->write_idx = filter->delay;
	filter->buffer = (float*) pMalloc(filter->capacity * sizeof(float));
	std::memset(filter->buffer, 0, filter->capacity * sizeof(float));
}

static inline void DspDelay_Change(DspDelay *filter, float delay_ms)
{
	assert(delay_ms >= 0 && delay_ms <= DSP_DELAY_MAX_DELAY_MS);

	/* Length */
	filter->delay = MsToSamples(delay_ms, filter->sampleRate);
	filter->read_idx = (filter->write_idx - filter->delay + filter->capacity) % filter->capacity;
}

static inline float DspDelay_Read(DspDelay *filter)
{
	float delay_out;

	assert(filter->read_idx < filter->capacity);

	delay_out = filter->buffer[filter->read_idx];
	filter->read_idx = (filter->read_idx + 1) % filter->capacity;
	return delay_out;
}

static inline void DspDelay_Write(DspDelay *filter, float sample)
{
	assert(filter->write_idx < filter->capacity);

	filter->buffer[filter->write_idx] = sample;
	filter->write_idx = (filter->write_idx + 1) % filter->capacity;
}

static inline float DspDelay_Process(DspDelay *filter, float sample_in)
{
	float delay_out = DspDelay_Read(filter);
	DspDelay_Write(filter, sample_in);
	return delay_out;
}

/* FIXME: This is currently unused! What was it for...? -flibit
static inline float DspDelay_Tap(DspDelay *filter, uint32_t delay)
{
	assert(delay <= filter->delay);
	return filter->buffer[(filter->write_idx - delay + filter->capacity) % filter->capacity];
}
*/

static inline void DspDelay_Reset(DspDelay *filter)
{
	filter->read_idx = 0;
	filter->write_idx = filter->delay;
	std::memset(filter->buffer, 0, filter->capacity * sizeof(float));
}

static inline void DspDelay_Destroy(DspDelay *filter, FAudioFreeFunc pFree)
{
	pFree(filter->buffer);
}

static inline float DspComb_FeedbackFromRT60(DspDelay *delay, float rt60_ms)
{
	float exponent = (
		(-3.0f * delay->delay * 1000.0f) /
		(delay->sampleRate * rt60_ms)
	);
	return (float) std::pow(10.0f, exponent);
}

/* Component - Bi-Quad Filter */

typedef enum DspBiQuadType
{
	DSP_BIQUAD_LOWSHELVING,
	DSP_BIQUAD_HIGHSHELVING
} DspBiQuadType;

typedef struct DspBiQuad
{
	int32_t sampleRate;
	float a0, a1, a2;
	float b1, b2;
	float c0, d0;
	float delay0, delay1;
} DspBiQuad;

static inline void DspBiQuad_Change(
	DspBiQuad *filter,
	DspBiQuadType type,
	float frequency,
	float q,
	float gain
) {
	const float TWOPI = (float)6.283185307179586476925286766559005;
	float theta_c = (TWOPI * frequency) / (float) filter->sampleRate;
	float mu = DbGainToFactor(gain);
	float beta = (type == DSP_BIQUAD_LOWSHELVING) ?
		4.0f / (1 + mu) :
		(1 + mu) / 4.0f;
	float delta = beta * (float) std::tan(theta_c * 0.5f);
	float gamma = (1 - delta) / (1 + delta);

	if (type == DSP_BIQUAD_LOWSHELVING)
	{
		filter->a0 = (1 - gamma) * 0.5f;
		filter->a1 = filter->a0;
	}
	else
	{
		filter->a0 = (1 + gamma) * 0.5f;
		filter->a1 = -filter->a0;
	}

	filter->a2 = 0.0f;
	filter->b1 = -gamma;
	filter->b2 = 0.0f;
	filter->c0 = mu - 1.0f;
	filter->d0 = 1.0f;
}

static inline void DspBiQuad_Initialize(
	DspBiQuad *filter,
	int32_t sampleRate,
	DspBiQuadType type,
	float frequency,	/* Corner frequency */
	float q,		/* Only used by low/high-pass filters */
	float gain		/* Only used by low/high-shelving filters */
) {
	filter->sampleRate = sampleRate;
	filter->delay0 = 0.0f;
	filter->delay1 = 0.0f;
	DspBiQuad_Change(filter, type, frequency, q, gain);
}

static inline float DspBiQuad_Process(DspBiQuad *filter, float sample_in)
{
	/* Direct Form II Transposed:
	 * - Less delay registers than Direct Form I
	 * - More numerically stable than Direct Form II
	 */
	float result = (filter->a0 * sample_in) + filter->delay0;
	filter->delay0 = (filter->a1 * sample_in) - (filter->b1 * result) + filter->delay1;
	filter->delay1 = (filter->a2 * sample_in) - (filter->b2 * result);

	return Undenormalize(
		(result * filter->c0) +
		(sample_in * filter->d0)
	);
}

static inline void DspBiQuad_Reset(DspBiQuad *filter)
{
	filter->delay0 = 0.0f;
	filter->delay1 = 0.0f;
}

static inline void DspBiQuad_Destroy(DspBiQuad *filter)
{
}

/* Component - Comb Filter with Integrated Low/High Shelving Filters */

typedef struct DspCombShelving
{
	DspDelay comb_delay;
	float comb_feedback_gain;

	DspBiQuad low_shelving;
	DspBiQuad high_shelving;
} DspCombShelving;

static inline void DspCombShelving_Initialize(
	DspCombShelving *filter,
	int32_t sampleRate,
	float delay_ms,
	float rt60_ms,
	float low_frequency,
	float low_gain,
	float high_frequency,
	float high_gain,
	FAudioMallocFunc pMalloc
) {
	DspDelay_Initialize(&filter->comb_delay, sampleRate, delay_ms, pMalloc);
	filter->comb_feedback_gain = DspComb_FeedbackFromRT60(
		&filter->comb_delay,
		rt60_ms
	);

	DspBiQuad_Initialize(
		&filter->low_shelving,
		sampleRate,
		DSP_BIQUAD_LOWSHELVING,
		low_frequency,
		0.0f,
		low_gain
	);
	DspBiQuad_Initialize(
		&filter->high_shelving,
		sampleRate,
		DSP_BIQUAD_HIGHSHELVING,
		high_frequency,
		0.0f,
		high_gain
	);
}

static inline float DspCombShelving_Process(
	DspCombShelving *filter,
	float sample_in
) {
	float delay_out, feedback, to_buf;

	delay_out = DspDelay_Read(&filter->comb_delay);

	/* Apply shelving filters */
	feedback = DspBiQuad_Process(&filter->high_shelving, delay_out);
	feedback = DspBiQuad_Process(&filter->low_shelving, feedback);

	/* Apply comb filter */
	to_buf = Undenormalize(sample_in + (filter->comb_feedback_gain * feedback));
	DspDelay_Write(&filter->comb_delay, to_buf);

	return delay_out;
}

static inline void DspCombShelving_Reset(DspCombShelving *filter)
{
	DspDelay_Reset(&filter->comb_delay);
	DspBiQuad_Reset(&filter->low_shelving);
	DspBiQuad_Reset(&filter->high_shelving);
}

static inline void DspCombShelving_Destroy(
	DspCombShelving *filter,
	FAudioFreeFunc pFree
) {
	DspDelay_Destroy(&filter->comb_delay, pFree);
	DspBiQuad_Destroy(&filter->low_shelving);
	DspBiQuad_Destroy(&filter->high_shelving);
}

/* Component - Delaying All-Pass Filter */

typedef struct DspAllPass
{
	DspDelay delay;
	float feedback_gain;
} DspAllPass;

static inline void DspAllPass_Initialize(
	DspAllPass *filter,
	int32_t sampleRate,
	float delay_ms,
	float gain,
	FAudioMallocFunc pMalloc
) {
	DspDelay_Initialize(&filter->delay, sampleRate, delay_ms, pMalloc);
	filter->feedback_gain = gain;
}

static inline void DspAllPass_Change(DspAllPass *filter, float delay_ms, float gain)
{
	DspDelay_Change(&filter->delay, delay_ms);
	filter->feedback_gain = gain;
}

static inline float DspAllPass_Process(DspAllPass *filter, float sample_in)
{
	float delay_out, to_buf;

	delay_out = DspDelay_Read(&filter->delay);

	to_buf = Undenormalize(sample_in + (filter->feedback_gain * delay_out));
	DspDelay_Write(&filter->delay, to_buf);

	return Undenormalize(delay_out - (filter->feedback_gain * to_buf));
}

static inline void DspAllPass_Reset(DspAllPass *filter)
{
	DspDelay_Reset(&filter->delay);
}

static inline void DspAllPass_Destroy(DspAllPass *filter, FAudioFreeFunc pFree)
{
	DspDelay_Destroy(&filter->delay, pFree);
}

/*
Reverb network - loosely based on the reverberator from
"Designing Audio Effect Plug-Ins in C++" by Will Pirkle and
the classic classic Schroeder-Moorer reverberator with modifications
to fit the XAudio2FX parameters.

                                                                         
    In    +--------+   +----+      +------------+      +-----+           
  ----|--->PreDelay---->APF1---+--->Sub LeftCh  |----->|     |   Left Out
      |   +--------+   +----+  |   +------------+      | Wet |-------->  
      |                        |   +------------+      |     |           
      |                        |---|Sub RightCh |----->| Dry |           
      |                            +------------+      |     |  Right Out
      |                                                | Mix |-------->  
      +----------------------------------------------->|     |           
                                                       +-----+           
 Sub routine per channel :                                                                    
                                                                         
     In  +-----+      +-----+ * cg                                       
   ---+->|Delay|--+---|Comb1|------+                                     
      |  +-----+  |   +-----+      |                                     
      |           |                |                                     
      |           |   +-----+ * cg |                                     
      |           +--->Comb2|------+                                     
      |           |   +-----+      |     +-----+                         
      |           |                +---->| SUM |--------+                
      |           |   +-----+ * cg |     +-----+        |                
      |           +--->...  |------+                    |                
      | * g0      |   +-----+      |                    |                
      |           |                |                    |                
      |           +--->-----+ * cg |                    |                
      |               |Comb8|------+                    |                
      |               +-----+                           |                
      v                                                 |                
   +-----+  g1   +----+   +----+   +----+   +----+      |                
   | SUM |<------|APF4|<--|APF3|<--|APF2|<--|APF1|<-----+                
   +-----+       +----+   +----+   +----+   +----+                       
      |                                                                  
      |                                                                  
      |            +-------------+          Out                          
      +----------->|RoomFilter   |------------------------>              
                   +-------------+                                       
                                                                         

Parameters:

float WetDryMix;		0 - 100 (0 = fully dry, 100 = fully wet)
uint32_t ReflectionsDelay;	0 - 300 ms
uint8_t ReverbDelay;		0 - 85 ms
uint8_t RearDelay;		0 - 5 ms
uint8_t PositionLeft;		0 - 30
uint8_t PositionRight;		0 - 30
uint8_t PositionMatrixLeft;	0 - 30
uint8_t PositionMatrixRight;	0 - 30
uint8_t EarlyDiffusion;		0 - 15
uint8_t LateDiffusion;		0 - 15
uint8_t LowEQGain;		0 - 12 (formula dB = LowEQGain - 8)
uint8_t LowEQCutoff;		0 - 9  (formula Hz = 50 + (LowEQCutoff * 50))
uint8_t HighEQGain;		0 - 8  (formula dB = HighEQGain - 8)
uint8_t HighEQCutoff;		0 - 14 (formula Hz = 1000 + (HighEqCutoff * 500))
float RoomFilterFreq;		20 - 20000Hz
float RoomFilterMain;		-100 - 0dB
float RoomFilterHF;		-100 - 0dB
float ReflectionsGain;		-100 - 20dB
float ReverbGain;		-100 - 20dB
float DecayTime;		0.1 - .... ms
float Density;			0 - 100 %
float RoomSize;			1 - 100 feet (NOT USED YET)

*/

#define REVERB_COUNT_COMB	8
#define REVERB_COUNT_APF_IN	1
#define REVERB_COUNT_APF_OUT	4

static float COMB_DELAYS[REVERB_COUNT_COMB] =
{
	25.31f,
	26.94f,
	28.96f,
	30.75f,
	32.24f,
	33.80f,
	35.31f,
	36.67f
};

static float APF_IN_DELAYS[REVERB_COUNT_APF_IN] =
{
	13.28f,
/*	28.13f */
};

static float APF_OUT_DELAYS[REVERB_COUNT_APF_OUT] =
{
	5.10f,
	12.61f,
	10.0f,
	7.73f
};

typedef enum FAudio_ChannelPositionFlags
{
	Position_Left = 0x1,
	Position_Right = 0x2,
	Position_Center = 0x4,
	Position_Rear = 0x8,
} FAudio_ChannelPositionFlags;

static FAudio_ChannelPositionFlags FAudio_GetChannelPositionFlags(int32_t total_channels, int32_t channel)
{
	switch (total_channels)
	{
		case 1:
			return Position_Center;

		case 2:
			return (channel == 0) ? Position_Left : Position_Right;

		case 4:
			switch (channel)
			{
				case 0:
					return Position_Left;
				case 1:
					return Position_Right;
				case 2:
					return static_cast<FAudio_ChannelPositionFlags>(Position_Left | Position_Rear);
				case 3:
					return static_cast<FAudio_ChannelPositionFlags>(Position_Right | Position_Rear);
			}
			assert(0 && "Unsupported channel count");
			break;
		case 5:
			switch (channel)
			{
				case 0:
					return Position_Left;
				case 1:
					return Position_Right;
				case 2:
					return Position_Center;
				case 3:
					return static_cast<FAudio_ChannelPositionFlags>(Position_Left | Position_Rear);
				case 4:
					return static_cast<FAudio_ChannelPositionFlags>(Position_Right | Position_Rear);
			}
			assert(0 && "Unsupported channel count");
			break;
		default:
			assert(0 && "Unsupported channel count");
			break;
	}

	/* shouldn't happen, but default to left speaker */
	return Position_Left;
}

static inline float FAudio_GetStereoSpreadDelayMS(int32_t total_channels, int32_t channel)
{
	FAudio_ChannelPositionFlags flags = FAudio_GetChannelPositionFlags(total_channels, channel);
	return (flags & Position_Right) ? 0.5216f : 0.0f;
}

typedef struct DspReverbChannel
{
	DspDelay reverb_delay;
	DspCombShelving	lpf_comb[REVERB_COUNT_COMB];
	DspAllPass apf_out[REVERB_COUNT_APF_OUT];
	DspBiQuad room_high_shelf;
	float early_gain;
	float gain;
} DspReverbChannel;

typedef struct DspReverb
{
	DspDelay early_delay;
	DspAllPass apf_in[REVERB_COUNT_APF_IN];
	
	int32_t in_channels;
	int32_t out_channels;
	int32_t reverb_channels;
	DspReverbChannel channel[5];

	float early_gain;
	float reverb_gain;
	float room_gain;
	float wet_ratio;
	float dry_ratio;
} DspReverb;

static inline void DspReverb_Create(
	DspReverb *reverb,
	int32_t sampleRate,
	int32_t in_channels,
	int32_t out_channels,
	FAudioMallocFunc pMalloc
) {
	int32_t i, c;

	assert(in_channels == 1 || in_channels == 2 || in_channels == 6);
	assert(out_channels == 1 || out_channels == 2 || out_channels == 6);

	std::memset(reverb, 0, sizeof(DspReverb));
	DspDelay_Initialize(&reverb->early_delay, sampleRate, 10, pMalloc);

	for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
	{
		DspAllPass_Initialize(
			&reverb->apf_in[i],
			sampleRate,
			APF_IN_DELAYS[i],
			0.5f,
			pMalloc
		);
	}

	if (out_channels == 6)
	{
		reverb->reverb_channels = (in_channels == 6) ? 5 : 4;
	}
	else
	{
		reverb->reverb_channels = out_channels;
	}

	for (c = 0; c < reverb->reverb_channels; c += 1)
	{
		DspDelay_Initialize(
			&reverb->channel[c].reverb_delay,
			sampleRate,
			10,
			pMalloc
		);

		for (i = 0; i < REVERB_COUNT_COMB; i += 1)
		{
			DspCombShelving_Initialize(
				&reverb->channel[c].lpf_comb[i],
				sampleRate,
				COMB_DELAYS[i] + FAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c),
				500,
				500,
				-6,
				5000,
				-6,
				pMalloc
			);
		}

		for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
		{
			DspAllPass_Initialize(
				&reverb->channel[c].apf_out[i],
				sampleRate,
				APF_OUT_DELAYS[i] + FAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c),
				0.5f,
				pMalloc
			);
		}

		DspBiQuad_Initialize(
			&reverb->channel[c].room_high_shelf,
			sampleRate,
			DSP_BIQUAD_HIGHSHELVING,
			5000,
			0,
			-10
		);
		reverb->channel[c].gain = 1.0f;
	}

	reverb->early_gain = 1.0f;
	reverb->reverb_gain = 1.0f;
	reverb->dry_ratio = 0.0f;
	reverb->wet_ratio = 1.0f;
	reverb->in_channels = in_channels;
	reverb->out_channels = out_channels;
}

static inline void DspReverb_Destroy(DspReverb *reverb, FAudioFreeFunc pFree)
{
	int32_t i, c;

	DspDelay_Destroy(&reverb->early_delay, pFree);

	for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
	{
		DspAllPass_Destroy(&reverb->apf_in[i], pFree);
	}

	for (c = 0; c < reverb->reverb_channels; c += 1)
	{
		DspDelay_Destroy(&reverb->channel[c].reverb_delay, pFree);

		for (i = 0; i < REVERB_COUNT_COMB; i += 1)
		{
			DspCombShelving_Destroy(
				&reverb->channel[c].lpf_comb[i],
				pFree
			);
		}

		DspBiQuad_Destroy(&reverb->channel[c].room_high_shelf);

		for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
		{
			DspAllPass_Destroy(
				&reverb->channel[c].apf_out[i],
				pFree
			);
		}
	}
}

static inline void DspReverb_SetParameters(
	DspReverb *reverb,
	FAudioFXReverbParameters *params
) {
	float early_diffusion, late_diffusion;
	DspCombShelving *comb;
	int32_t i, c;

	/* Pre-Delay */
	DspDelay_Change(&reverb->early_delay, (float) params->ReflectionsDelay);

	/* Early Reflections - Diffusion */
	early_diffusion = 0.6f - ((params->EarlyDiffusion / 15.0f) * 0.2f);

	for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
	{
		DspAllPass_Change(
			&reverb->apf_in[i],
			APF_IN_DELAYS[i],
			early_diffusion
		);
	}

	/* Reverberation */
	for (c = 0; c < reverb->reverb_channels; c += 1)
	{
		float channel_delay =
			(FAudio_GetChannelPositionFlags(reverb->reverb_channels, c) & Position_Rear) ?
			params->RearDelay :
			0.0f;

		DspDelay_Change(
			&reverb->channel[c].reverb_delay,
			(float) params->ReverbDelay + channel_delay
		);

		for (i = 0; i < REVERB_COUNT_COMB; i += 1)
		{
			comb = &reverb->channel[c].lpf_comb[i];

			/* Set decay time of comb filter */
			DspDelay_Change(
				&comb->comb_delay,
				COMB_DELAYS[i] + FAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c)
			);
			comb->comb_feedback_gain = DspComb_FeedbackFromRT60(
				&comb->comb_delay,
				std::max(params->DecayTime, FAUDIOFX_REVERB_MIN_DECAY_TIME) * 1000.0f
			);

			/* High/Low shelving */
			DspBiQuad_Change(
				&comb->low_shelving,
				DSP_BIQUAD_LOWSHELVING,
				50.0f + params->LowEQCutoff * 50.0f,
				0.0f,
				params->LowEQGain - 8.0f
			);
			DspBiQuad_Change(
				&comb->high_shelving,
				DSP_BIQUAD_HIGHSHELVING,
				1000 + params->HighEQCutoff * 500.0f,
				0.0f,
				params->HighEQGain - 8.0f
			);
		}
	}

	/* Gain */
	reverb->early_gain = DbGainToFactor(params->ReflectionsGain);
	reverb->reverb_gain = DbGainToFactor(params->ReverbGain);
	reverb->room_gain = DbGainToFactor(params->RoomFilterMain);

	/* Late Diffusion */
	late_diffusion = 0.6f - ((params->LateDiffusion / 15.0f) * 0.2f);

	for (c = 0; c < reverb->reverb_channels; c += 1)
	{
		FAudio_ChannelPositionFlags position = FAudio_GetChannelPositionFlags(reverb->reverb_channels, c);
		float gain;

		for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
		{
			DspAllPass_Change(
				&reverb->channel[c].apf_out[i],
				APF_OUT_DELAYS[i] + FAudio_GetStereoSpreadDelayMS(reverb->reverb_channels, c),
				late_diffusion
			);
		}

		DspBiQuad_Change(
			&reverb->channel[c].room_high_shelf,
			DSP_BIQUAD_HIGHSHELVING,
			params->RoomFilterFreq,
			0.0f,
			params->RoomFilterMain + params->RoomFilterHF
		);

		if (position & Position_Left)
		{
			gain = params->PositionMatrixLeft;
		}
		else if (position & Position_Right)
		{
			gain = params->PositionMatrixRight;
		}
		else /*if (position & Position_Center) */
		{
			gain = (params->PositionMatrixLeft + params->PositionMatrixRight) / 2.0f;
		}
		reverb->channel[c].gain = 1.5f - (gain / 27.0f) * 0.5f;

		if (position & Position_Rear)
		{
			/* Rear-channel Attenuation */
			reverb->channel[c].gain *= 0.75f;
		}

		if (position & Position_Left)
		{
			gain = params->PositionLeft;
		}
		else if (position & Position_Right)
		{
			gain = params->PositionRight;
		}
		else /*if (position & Position_Center) */
		{
			gain = (params->PositionLeft + params->PositionRight) / 2.0f;
		}
		reverb->channel[c].early_gain = 1.2f - (gain / 6.0f) * 0.2f;
		reverb->channel[c].early_gain = (
			reverb->channel[c].early_gain *
			reverb->early_gain
		);
	}

	/* Wet/Dry Mix (100 = fully wet, 0 = fully dry) */
	reverb->wet_ratio = params->WetDryMix / 100.0f;
	reverb->dry_ratio = 1.0f - reverb->wet_ratio;
}

static inline float DspReverb_INTERNAL_ProcessEarly(
	DspReverb *reverb,
	float sample_in
) {
	float early;
	int32_t i;

	/* Pre-Delay */
	early = DspDelay_Process(&reverb->early_delay, sample_in);

	/* Early Reflections */
	for (i = 0; i < REVERB_COUNT_APF_IN; i += 1)
	{
		early = DspAllPass_Process(&reverb->apf_in[i], early);
	}

	return early;
}

static inline float DspReverb_INTERNAL_ProcessChannel(
	DspReverb *reverb,
	DspReverbChannel *channel,
	float sample_in
) {
	float revdelay, early_late, sample_out;
	int32_t i;

	revdelay = DspDelay_Process(&channel->reverb_delay, sample_in);

	sample_out = 0.0f;
	for (i = 0; i < REVERB_COUNT_COMB; i += 1)
	{
		sample_out += DspCombShelving_Process(
			&channel->lpf_comb[i],
			revdelay
		);
	}
	sample_out /= (float) REVERB_COUNT_COMB;

	/* Output Diffusion */
	for (i = 0; i < REVERB_COUNT_APF_OUT; i += 1)
	{
		sample_out = DspAllPass_Process(
			&channel->apf_out[i],
			sample_out
		);
	}

	/* Combine early reflections and reverberation */
	early_late = (
		(sample_in * channel->early_gain) +
		(sample_out * reverb->reverb_gain)
	);

	/* Room filter */
	sample_out = DspBiQuad_Process(
		&channel->room_high_shelf,
		early_late * reverb->room_gain
	);

	/* PositionMatrixLeft/Right */
	return sample_out * channel->gain;
}

/* Reverb Process Functions */

static inline float DspReverb_INTERNAL_Process_1_to_1(
	DspReverb *reverb,
	float *samples_in,
	float *samples_out,
	size_t sample_count
) {
	const float *in_end = samples_in + sample_count;
	float in, early, late, out;
	float squared_sum = 0.0f;

	while (samples_in < in_end)
	{
		/* Input */
		in = *samples_in++;

		/* Early Reflections */
		early = DspReverb_INTERNAL_ProcessEarly(reverb, in);

		/* Reverberation */
		late = DspReverb_INTERNAL_ProcessChannel(
			reverb,
			&reverb->channel[0],
			early
		);

		/* Wet/Dry Mix */
		out = (late * reverb->wet_ratio) + (in * reverb->dry_ratio);
		squared_sum += out * out;

		/* Output */
		*samples_out++ = out;
	}

	return squared_sum;
}


static inline void Reset(DspReverb& r) {
 DspDelay_Reset(&r.early_delay);
 for(auto& a:r.apf_in)DspAllPass_Reset(&a);
 for(int c=0;c<r.reverb_channels;++c) {
  auto& ch=r.channel[c]; DspDelay_Reset(&ch.reverb_delay);
  for(auto& comb:ch.lpf_comb)DspCombShelving_Reset(&comb);
  DspBiQuad_Reset(&ch.room_high_shelf);
  for(auto& a:ch.apf_out)DspAllPass_Reset(&a);
 }
}
} // namespace CNA::ThirdParty::FAudioReverb
