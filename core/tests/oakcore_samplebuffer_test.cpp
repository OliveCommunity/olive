/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "olive/core/oakcore/samplebuffer.h"

/* These functions are declared in oakcore/audioparams.h (a parallel
 * delivery); they are re-declared here so that this test only includes the
 * samplebuffer C header it is meant to exercise. */
extern "C" {
OakAudioParams *oakcore_audioparams_create(int sample_rate,
										   uint64_t channel_layout,
										   int format);
void oakcore_audioparams_free(OakAudioParams *self);
int oakcore_audioparams_sample_rate(const OakAudioParams *self);
int oakcore_audioparams_channel_count(const OakAudioParams *self);
}

/* Values mirror render/channellayout.h (k_channel_layout_stereo) and
 * render/sampleformat.h (SampleFormat::f32_p) on the library side. */
#define SAMPLE_RATE 48000
#define CHANNEL_LAYOUT_STEREO 0x3
#define FORMAT_F32P 4

static int float_eq(float a, float b)
{
	return fabsf(a - b) < 1e-6f;
}

int main(void)
{
	/* --- Default construction: empty, unallocated --- */
	OakSampleBuffer *def = oakcore_samplebuffer_create();
	assert(def != NULL);
	assert(oakcore_samplebuffer_is_allocated(def) == 0);
	assert(oakcore_samplebuffer_sample_count(def) == 0);
	assert(oakcore_samplebuffer_channel_count(def) == 0);
	assert(oakcore_samplebuffer_data(def, 0) == NULL);

	/* audio_params() hands back an owned copy of the (invalid) defaults */
	OakAudioParams *def_params = oakcore_samplebuffer_audio_params(def);
	assert(def_params != NULL);
	assert(oakcore_audioparams_sample_rate(def_params) == 0);
	oakcore_audioparams_free(def_params);

	/* Operations that need an allocation warn and leave the buffer alone */
	oakcore_samplebuffer_allocate(def);
	assert(oakcore_samplebuffer_is_allocated(def) == 0);
	oakcore_samplebuffer_silence(def);
	oakcore_samplebuffer_reverse(def);
	assert(oakcore_samplebuffer_is_allocated(def) == 0);
	oakcore_samplebuffer_free(def);

	/* --- Construction from audio params + sample count --- */
	OakAudioParams *stereo = oakcore_audioparams_create(
		SAMPLE_RATE, CHANNEL_LAYOUT_STEREO, FORMAT_F32P);
	assert(stereo != NULL);

	OakSampleBuffer *buf = oakcore_samplebuffer_create_samples(stereo, 100);
	assert(oakcore_samplebuffer_is_allocated(buf) == 1);
	assert(oakcore_samplebuffer_sample_count(buf) == 100);
	assert(oakcore_samplebuffer_channel_count(buf) == 2);

	/* A fresh allocation is silent */
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(buf, ch);
		assert(d != NULL);
		for (int i = 0; i < 100; i++) {
			assert(float_eq(d[i], 0.0f));
		}
	}

	/* audio_params() copy reflects the construction parameters */
	OakAudioParams *p = oakcore_samplebuffer_audio_params(buf);
	assert(oakcore_audioparams_sample_rate(p) == SAMPLE_RATE);
	assert(oakcore_audioparams_channel_count(p) == 2);
	oakcore_audioparams_free(p);

	/* data() rejects out-of-range channels */
	assert(oakcore_samplebuffer_data(buf, -1) == NULL);
	assert(oakcore_samplebuffer_data(buf, 2) == NULL);

	/* --- Construction from audio params + rational length (0.5s) --- */
	OakRational *half_sec = oakcore_rational_create_nd(1, 2);
	OakSampleBuffer *timed =
		oakcore_samplebuffer_create_length(stereo, half_sec);
	assert(oakcore_samplebuffer_is_allocated(timed) == 1);
	assert(oakcore_samplebuffer_sample_count(timed) == 24000);
	oakcore_rational_free(half_sec);
	oakcore_samplebuffer_free(timed);

	/* --- Manual lifecycle: params + count + allocate, destroy, repeat --- */
	OakSampleBuffer *manual = oakcore_samplebuffer_create();
	oakcore_samplebuffer_set_audio_params(manual, stereo);
	oakcore_samplebuffer_set_sample_count(manual, 50);
	oakcore_samplebuffer_allocate(manual);
	assert(oakcore_samplebuffer_is_allocated(manual) == 1);
	assert(oakcore_samplebuffer_sample_count(manual) == 50);
	assert(oakcore_samplebuffer_channel_count(manual) == 2);

	oakcore_samplebuffer_destroy(manual);
	assert(oakcore_samplebuffer_is_allocated(manual) == 0);
	assert(oakcore_samplebuffer_channel_count(manual) == 0);
	assert(oakcore_samplebuffer_data(manual, 0) == NULL);

	oakcore_samplebuffer_set_sample_count(manual, 30);
	oakcore_samplebuffer_allocate(manual);
	assert(oakcore_samplebuffer_is_allocated(manual) == 1);
	assert(oakcore_samplebuffer_sample_count(manual) == 30);
	oakcore_samplebuffer_free(manual);

	/* set_sample_count via a rational length: 0.001s at 48kHz = 48 samples */
	OakSampleBuffer *manual2 = oakcore_samplebuffer_create();
	oakcore_samplebuffer_set_audio_params(manual2, stereo);
	OakRational *one_ms = oakcore_rational_create_nd(1, 1000);
	oakcore_samplebuffer_set_sample_count_length(manual2, one_ms);
	oakcore_rational_free(one_ms);
	oakcore_samplebuffer_allocate(manual2);
	assert(oakcore_samplebuffer_sample_count(manual2) == 48);
	oakcore_samplebuffer_free(manual2);

	/* --- data() direct access, set(), to_raw_ptrs() --- */
	float *ch0 = oakcore_samplebuffer_data(buf, 0);
	float *ch1 = oakcore_samplebuffer_data(buf, 1);
	for (int i = 0; i < 100; i++) {
		ch0[i] = (float)i;       /* ramp 0..99 on channel 0 */
		ch1[i] = (float)(i * 10); /* ramp 0..990 on channel 1 */
	}

	const float ins[4] = { -1.0f, -2.0f, -3.0f, -4.0f };
	oakcore_samplebuffer_set(buf, 0, ins, 10, 4); /* write at offset 10 */
	for (int k = 0; k < 4; k++) {
		assert(float_eq(ch0[10 + k], ins[k]));
	}
	assert(float_eq(ch0[9], 9.0f)); /* neighbours untouched */
	assert(float_eq(ch0[14], 14.0f));

	oakcore_samplebuffer_set(buf, 1, ins, 0, 4); /* write at offset 0 */
	for (int k = 0; k < 4; k++) {
		assert(float_eq(ch1[k], ins[k]));
	}
	for (int k = 0; k < 4; k++) { /* restore the channel 1 ramp */
		ch1[k] = (float)(k * 10);
	}

	float *ptrs[2] = { NULL, NULL };
	oakcore_samplebuffer_to_raw_ptrs(buf, ptrs);
	assert(ptrs[0] == ch0);
	assert(ptrs[1] == ch1);
	ptrs[1][50] = 123.0f; /* writes through the raw pointer land in the buffer */
	assert(float_eq(oakcore_samplebuffer_data(buf, 1)[50], 123.0f));
	ch1[50] = 500.0f; /* restore ramp value (50 * 10) */

	/* --- copy: an independent deep copy --- */
	OakSampleBuffer *cp = oakcore_samplebuffer_copy(buf);
	assert(oakcore_samplebuffer_is_allocated(cp) == 1);
	assert(oakcore_samplebuffer_sample_count(cp) == 100);
	assert(oakcore_samplebuffer_channel_count(cp) == 2);
	assert(float_eq(oakcore_samplebuffer_data(cp, 0)[9], 9.0f));

	/* --- transform_volume (in place, all channels) --- */
	oakcore_samplebuffer_transform_volume(cp, 2.0f);
	assert(float_eq(oakcore_samplebuffer_data(cp, 0)[9], 18.0f));
	assert(float_eq(oakcore_samplebuffer_data(cp, 1)[9], 180.0f));
	assert(float_eq(oakcore_samplebuffer_data(buf, 0)[9], 9.0f)); /* source keeps its data */

	/* --- transform_volume_for_channel (in place, one channel) --- */
	oakcore_samplebuffer_transform_volume_for_channel(cp, 1, 0.5f);
	assert(float_eq(oakcore_samplebuffer_data(cp, 1)[9], 90.0f));
	assert(float_eq(oakcore_samplebuffer_data(cp, 0)[9], 18.0f)); /* other channel untouched */
	oakcore_samplebuffer_free(cp);

	/* --- static-style transforms: input -> output --- */
	OakSampleBuffer *tin = oakcore_samplebuffer_create_samples(stereo, 10);
	OakSampleBuffer *tout = oakcore_samplebuffer_create_samples(stereo, 10);
	for (int i = 0; i < 10; i++) {
		oakcore_samplebuffer_data(tin, 0)[i] = (float)i;
		oakcore_samplebuffer_data(tin, 1)[i] = (float)(100 + i);
	}

	oakcore_samplebuffer_transform_volume_to(0.5f, tin, tout);
	for (int i = 0; i < 10; i++) {
		assert(float_eq(oakcore_samplebuffer_data(tout, 0)[i], (float)i * 0.5f));
		assert(float_eq(oakcore_samplebuffer_data(tout, 1)[i],
						(float)(100 + i) * 0.5f));
		assert(float_eq(oakcore_samplebuffer_data(tin, 0)[i], (float)i)); /* input unchanged */
	}

	oakcore_samplebuffer_silence(tout);
	oakcore_samplebuffer_transform_volume_for_channel_to(1, 2.0f, tin, tout);
	for (int i = 0; i < 10; i++) {
		assert(float_eq(oakcore_samplebuffer_data(tout, 1)[i],
						(float)(100 + i) * 2.0f));
		assert(float_eq(oakcore_samplebuffer_data(tout, 0)[i], 0.0f)); /* channel 0 untouched */
	}

	/* --- per-sample volume transforms --- */
	oakcore_samplebuffer_transform_volume_for_sample(tin, 3, 10.0f);
	assert(float_eq(oakcore_samplebuffer_data(tin, 0)[3], 30.0f));
	assert(float_eq(oakcore_samplebuffer_data(tin, 1)[3], 1030.0f));

	oakcore_samplebuffer_transform_volume_for_sample_on_channel(tin, 4, 0,
																100.0f);
	assert(float_eq(oakcore_samplebuffer_data(tin, 0)[4], 400.0f));
	assert(float_eq(oakcore_samplebuffer_data(tin, 1)[4], 104.0f)); /* channel 1 untouched */
	oakcore_samplebuffer_free(tin);
	oakcore_samplebuffer_free(tout);

	/* --- clamp() limits every channel to [-1, 1] --- */
	OakSampleBuffer *cl = oakcore_samplebuffer_create_samples(stereo, 4);
	oakcore_samplebuffer_data(cl, 0)[0] = 2.5f;
	oakcore_samplebuffer_data(cl, 0)[1] = -2.5f;
	oakcore_samplebuffer_data(cl, 1)[0] = 42.0f;
	oakcore_samplebuffer_data(cl, 1)[1] = 0.25f;
	oakcore_samplebuffer_clamp(cl);
	assert(float_eq(oakcore_samplebuffer_data(cl, 0)[0], 1.0f));
	assert(float_eq(oakcore_samplebuffer_data(cl, 0)[1], -1.0f));
	assert(float_eq(oakcore_samplebuffer_data(cl, 1)[0], 1.0f));
	assert(float_eq(oakcore_samplebuffer_data(cl, 1)[1], 0.25f));
	oakcore_samplebuffer_free(cl);

	/* --- silence / silence_range / silence_bytes --- */
	OakSampleBuffer *si = oakcore_samplebuffer_create_samples(stereo, 10);
	for (int ch = 0; ch < 2; ch++) {
		for (int i = 0; i < 10; i++) {
			oakcore_samplebuffer_data(si, ch)[i] = 1.0f;
		}
	}

	oakcore_samplebuffer_silence_range(si, 2, 5); /* samples [2, 5) */
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(si, ch);
		assert(float_eq(d[1], 1.0f));
		assert(float_eq(d[2], 0.0f));
		assert(float_eq(d[4], 0.0f));
		assert(float_eq(d[5], 1.0f));
	}

	oakcore_samplebuffer_silence_bytes(si, 0, 2 * sizeof(float)); /* samples [0, 2) */
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(si, ch);
		assert(float_eq(d[0], 0.0f));
		assert(float_eq(d[1], 0.0f));
		assert(float_eq(d[5], 1.0f));
	}

	oakcore_samplebuffer_silence(si); /* everything */
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(si, ch);
		for (int i = 0; i < 10; i++) {
			assert(float_eq(d[i], 0.0f));
		}
	}
	oakcore_samplebuffer_free(si);

	/* --- reverse() flips every channel --- */
	OakSampleBuffer *rv = oakcore_samplebuffer_create_samples(stereo, 5);
	for (int i = 0; i < 5; i++) {
		oakcore_samplebuffer_data(rv, 0)[i] = (float)i;
		oakcore_samplebuffer_data(rv, 1)[i] = (float)(10 * i);
	}
	oakcore_samplebuffer_reverse(rv);
	for (int i = 0; i < 5; i++) {
		assert(float_eq(oakcore_samplebuffer_data(rv, 0)[i], (float)(4 - i)));
		assert(float_eq(oakcore_samplebuffer_data(rv, 1)[i],
						(float)(10 * (4 - i))));
	}
	oakcore_samplebuffer_free(rv);

	/* --- speed(2.0) halves the sample count, sampling the ramp exactly --- */
	OakSampleBuffer *sp = oakcore_samplebuffer_create_samples(stereo, 100);
	for (int i = 0; i < 100; i++) {
		oakcore_samplebuffer_data(sp, 0)[i] = (float)i;
	}
	oakcore_samplebuffer_speed(sp, 2.0);
	assert(oakcore_samplebuffer_sample_count(sp) == 50);
	for (int i = 0; i < 50; i++) {
		assert(float_eq(oakcore_samplebuffer_data(sp, 0)[i], (float)(2 * i)));
	}
	oakcore_samplebuffer_free(sp);

	/* --- fast_set(): channel copy between buffers --- */
	OakSampleBuffer *fa = oakcore_samplebuffer_create_samples(stereo, 10);
	OakSampleBuffer *fb = oakcore_samplebuffer_create_samples(stereo, 10);
	for (int i = 0; i < 10; i++) {
		oakcore_samplebuffer_data(fa, 0)[i] = (float)i;
		oakcore_samplebuffer_data(fa, 1)[i] = (float)(100 + i);
	}

	oakcore_samplebuffer_fast_set(fb, fa, 1, 0); /* fb channel 1 <- fa channel 0 */
	for (int i = 0; i < 10; i++) {
		assert(float_eq(oakcore_samplebuffer_data(fb, 1)[i], (float)i));
		assert(float_eq(oakcore_samplebuffer_data(fb, 0)[i], 0.0f));
	}

	oakcore_samplebuffer_fast_set(fb, fa, 0, -1); /* from == -1 mirrors to */
	for (int i = 0; i < 10; i++) {
		assert(float_eq(oakcore_samplebuffer_data(fb, 0)[i], (float)i));
	}
	oakcore_samplebuffer_free(fa);
	oakcore_samplebuffer_free(fb);

	/* --- rip_channel(): a new mono buffer with one channel's samples --- */
	OakSampleBuffer *rip = oakcore_samplebuffer_rip_channel(buf, 1);
	assert(oakcore_samplebuffer_is_allocated(rip) == 1);
	assert(oakcore_samplebuffer_channel_count(rip) == 1);
	assert(oakcore_samplebuffer_sample_count(rip) == 100);
	for (int i = 0; i < 100; i++) {
		assert(float_eq(oakcore_samplebuffer_data(rip, 0)[i], (float)(i * 10)));
	}
	OakAudioParams *rip_params = oakcore_samplebuffer_audio_params(rip);
	assert(oakcore_audioparams_sample_rate(rip_params) == SAMPLE_RATE);
	assert(oakcore_audioparams_channel_count(rip_params) == 1);
	oakcore_audioparams_free(rip_params);
	oakcore_samplebuffer_free(rip);

	/* --- rip_channel_vector(): query size, partial copy, full copy --- */
	assert(oakcore_samplebuffer_rip_channel_vector(buf, 1, NULL, 0) == 100);

	float partial[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	assert(oakcore_samplebuffer_rip_channel_vector(buf, 1, partial, 4) == 100);
	for (int k = 0; k < 4; k++) {
		assert(float_eq(partial[k], (float)(k * 10)));
	}

	float full[100];
	assert(oakcore_samplebuffer_rip_channel_vector(buf, 1, full, 100) == 100);
	for (int i = 0; i < 100; i++) {
		assert(float_eq(full[i], (float)(i * 10)));
	}

	/* --- Ownership: everything released --- */
	oakcore_samplebuffer_free(buf);
	oakcore_audioparams_free(stereo);

	printf("oakcore_samplebuffer_test: all assertions passed\n");
	return 0;
}
