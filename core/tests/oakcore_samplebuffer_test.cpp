#include <gtest/gtest.h>
#include <math.h>
#include <stdint.h>

#include "olive/core/oakcore/samplebuffer.h"

extern "C" {
OakAudioParams *oakcore_audioparams_create(int sample_rate,
										   uint64_t channel_layout,
										   int format);
void oakcore_audioparams_free(OakAudioParams *self);
int oakcore_audioparams_sample_rate(const OakAudioParams *self);
int oakcore_audioparams_channel_count(const OakAudioParams *self);
}

#define SAMPLE_RATE 48000
#define CHANNEL_LAYOUT_STEREO 0x3
#define FORMAT_F32P 4

static bool feq(float a, float b) { return fabsf(a - b) < 1e-6f; }

class OakcoreSampleBufferTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		stereo_ = oakcore_audioparams_create(SAMPLE_RATE, CHANNEL_LAYOUT_STEREO, FORMAT_F32P);
	}
	void TearDown() override
	{
		oakcore_audioparams_free(stereo_);
	}
	OakAudioParams *stereo_;
};

TEST(OakcoreSampleBuffer, DefaultConstruction)
{
	OakSampleBuffer *def = oakcore_samplebuffer_create();
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(def), 0);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(def), 0);
	EXPECT_EQ(oakcore_samplebuffer_channel_count(def), 0);
	EXPECT_EQ(oakcore_samplebuffer_data(def, 0), nullptr);

	OakAudioParams *def_params = oakcore_samplebuffer_audio_params(def);
	ASSERT_NE(def_params, nullptr);
	EXPECT_EQ(oakcore_audioparams_sample_rate(def_params), 0);
	oakcore_audioparams_free(def_params);

	// Operations that need an allocation warn and leave the buffer alone
	oakcore_samplebuffer_allocate(def);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(def), 0);
	oakcore_samplebuffer_silence(def);
	oakcore_samplebuffer_reverse(def);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(def), 0);
	oakcore_samplebuffer_free(def);
}

TEST_F(OakcoreSampleBufferTest, CreateFromParamsAndCount)
{
	OakSampleBuffer *buf = oakcore_samplebuffer_create_samples(stereo_, 100);
	ASSERT_NE(buf, nullptr);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(buf), 1);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(buf), 100);
	EXPECT_EQ(oakcore_samplebuffer_channel_count(buf), 2);

	// A fresh allocation is silent
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(buf, ch);
		ASSERT_NE(d, nullptr);
		for (int i = 0; i < 100; i++) {
			EXPECT_TRUE(feq(d[i], 0.0f));
		}
	}

	OakAudioParams *p = oakcore_samplebuffer_audio_params(buf);
	EXPECT_EQ(oakcore_audioparams_sample_rate(p), SAMPLE_RATE);
	EXPECT_EQ(oakcore_audioparams_channel_count(p), 2);
	oakcore_audioparams_free(p);

	// data() rejects out-of-range channels
	EXPECT_EQ(oakcore_samplebuffer_data(buf, -1), nullptr);
	EXPECT_EQ(oakcore_samplebuffer_data(buf, 2), nullptr);
	oakcore_samplebuffer_free(buf);
}

TEST_F(OakcoreSampleBufferTest, CreateFromRationalLength)
{
	OakRational *half_sec = oakcore_rational_create_nd(1, 2);
	OakSampleBuffer *timed = oakcore_samplebuffer_create_length(stereo_, half_sec);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(timed), 1);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(timed), 24000);
	oakcore_rational_free(half_sec);
	oakcore_samplebuffer_free(timed);
}

TEST_F(OakcoreSampleBufferTest, ManualLifecycle)
{
	OakSampleBuffer *manual = oakcore_samplebuffer_create();
	oakcore_samplebuffer_set_audio_params(manual, stereo_);
	oakcore_samplebuffer_set_sample_count(manual, 50);
	oakcore_samplebuffer_allocate(manual);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(manual), 1);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(manual), 50);
	EXPECT_EQ(oakcore_samplebuffer_channel_count(manual), 2);

	oakcore_samplebuffer_destroy(manual);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(manual), 0);
	EXPECT_EQ(oakcore_samplebuffer_channel_count(manual), 0);
	EXPECT_EQ(oakcore_samplebuffer_data(manual, 0), nullptr);

	oakcore_samplebuffer_set_sample_count(manual, 30);
	oakcore_samplebuffer_allocate(manual);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(manual), 1);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(manual), 30);
	oakcore_samplebuffer_free(manual);

	// set_sample_count via a rational length: 0.001s at 48kHz = 48 samples
	OakSampleBuffer *manual2 = oakcore_samplebuffer_create();
	oakcore_samplebuffer_set_audio_params(manual2, stereo_);
	OakRational *one_ms = oakcore_rational_create_nd(1, 1000);
	oakcore_samplebuffer_set_sample_count_length(manual2, one_ms);
	oakcore_rational_free(one_ms);
	oakcore_samplebuffer_allocate(manual2);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(manual2), 48);
	oakcore_samplebuffer_free(manual2);
}

TEST_F(OakcoreSampleBufferTest, DataAccessSetAndRawPtrs)
{
	OakSampleBuffer *buf = oakcore_samplebuffer_create_samples(stereo_, 100);
	float *ch0 = oakcore_samplebuffer_data(buf, 0);
	float *ch1 = oakcore_samplebuffer_data(buf, 1);
	for (int i = 0; i < 100; i++) {
		ch0[i] = (float)i;
		ch1[i] = (float)(i * 10);
	}

	const float ins[4] = { -1.0f, -2.0f, -3.0f, -4.0f };
	oakcore_samplebuffer_set(buf, 0, ins, 10, 4);
	for (int k = 0; k < 4; k++) {
		EXPECT_TRUE(feq(ch0[10 + k], ins[k]));
	}
	EXPECT_TRUE(feq(ch0[9], 9.0f));
	EXPECT_TRUE(feq(ch0[14], 14.0f));

	oakcore_samplebuffer_set(buf, 1, ins, 0, 4);
	for (int k = 0; k < 4; k++) {
		EXPECT_TRUE(feq(ch1[k], ins[k]));
	}
	for (int k = 0; k < 4; k++) {
		ch1[k] = (float)(k * 10);
	}

	float *ptrs[2] = { nullptr, nullptr };
	oakcore_samplebuffer_to_raw_ptrs(buf, ptrs);
	EXPECT_EQ(ptrs[0], ch0);
	EXPECT_EQ(ptrs[1], ch1);
	ptrs[1][50] = 123.0f;
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(buf, 1)[50], 123.0f));
	ch1[50] = 500.0f;
	oakcore_samplebuffer_free(buf);
}

TEST_F(OakcoreSampleBufferTest, CopyAndVolumeTransform)
{
	OakSampleBuffer *buf = oakcore_samplebuffer_create_samples(stereo_, 100);
	float *ch0 = oakcore_samplebuffer_data(buf, 0);
	float *ch1 = oakcore_samplebuffer_data(buf, 1);
	for (int i = 0; i < 100; i++) {
		ch0[i] = (float)i;
		ch1[i] = (float)(i * 10);
	}

	OakSampleBuffer *cp = oakcore_samplebuffer_copy(buf);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(cp), 1);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(cp), 100);
	EXPECT_EQ(oakcore_samplebuffer_channel_count(cp), 2);
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cp, 0)[9], 9.0f));

	oakcore_samplebuffer_transform_volume(cp, 2.0f);
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cp, 0)[9], 18.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cp, 1)[9], 180.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(buf, 0)[9], 9.0f));

	oakcore_samplebuffer_transform_volume_for_channel(cp, 1, 0.5f);
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cp, 1)[9], 90.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cp, 0)[9], 18.0f));
	oakcore_samplebuffer_free(cp);
	oakcore_samplebuffer_free(buf);
}

TEST_F(OakcoreSampleBufferTest, StaticVolumeTransforms)
{
	OakSampleBuffer *tin = oakcore_samplebuffer_create_samples(stereo_, 10);
	OakSampleBuffer *tout = oakcore_samplebuffer_create_samples(stereo_, 10);
	for (int i = 0; i < 10; i++) {
		oakcore_samplebuffer_data(tin, 0)[i] = (float)i;
		oakcore_samplebuffer_data(tin, 1)[i] = (float)(100 + i);
	}

	oakcore_samplebuffer_transform_volume_to(0.5f, tin, tout);
	for (int i = 0; i < 10; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(tout, 0)[i], (float)i * 0.5f));
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(tout, 1)[i], (float)(100 + i) * 0.5f));
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(tin, 0)[i], (float)i));
	}

	oakcore_samplebuffer_silence(tout);
	oakcore_samplebuffer_transform_volume_for_channel_to(1, 2.0f, tin, tout);
	for (int i = 0; i < 10; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(tout, 1)[i], (float)(100 + i) * 2.0f));
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(tout, 0)[i], 0.0f));
	}

	// per-sample volume transforms
	oakcore_samplebuffer_transform_volume_for_sample(tin, 3, 10.0f);
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(tin, 0)[3], 30.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(tin, 1)[3], 1030.0f));

	oakcore_samplebuffer_transform_volume_for_sample_on_channel(tin, 4, 0, 100.0f);
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(tin, 0)[4], 400.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(tin, 1)[4], 104.0f));
	oakcore_samplebuffer_free(tin);
	oakcore_samplebuffer_free(tout);
}

TEST_F(OakcoreSampleBufferTest, Clamp)
{
	OakSampleBuffer *cl = oakcore_samplebuffer_create_samples(stereo_, 4);
	oakcore_samplebuffer_data(cl, 0)[0] = 2.5f;
	oakcore_samplebuffer_data(cl, 0)[1] = -2.5f;
	oakcore_samplebuffer_data(cl, 1)[0] = 42.0f;
	oakcore_samplebuffer_data(cl, 1)[1] = 0.25f;
	oakcore_samplebuffer_clamp(cl);
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cl, 0)[0], 1.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cl, 0)[1], -1.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cl, 1)[0], 1.0f));
	EXPECT_TRUE(feq(oakcore_samplebuffer_data(cl, 1)[1], 0.25f));
	oakcore_samplebuffer_free(cl);
}

TEST_F(OakcoreSampleBufferTest, SilenceAndSilenceRange)
{
	OakSampleBuffer *si = oakcore_samplebuffer_create_samples(stereo_, 10);
	for (int ch = 0; ch < 2; ch++) {
		for (int i = 0; i < 10; i++) {
			oakcore_samplebuffer_data(si, ch)[i] = 1.0f;
		}
	}

	oakcore_samplebuffer_silence_range(si, 2, 5);
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(si, ch);
		EXPECT_TRUE(feq(d[1], 1.0f));
		EXPECT_TRUE(feq(d[2], 0.0f));
		EXPECT_TRUE(feq(d[4], 0.0f));
		EXPECT_TRUE(feq(d[5], 1.0f));
	}

	oakcore_samplebuffer_silence_bytes(si, 0, 2 * sizeof(float));
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(si, ch);
		EXPECT_TRUE(feq(d[0], 0.0f));
		EXPECT_TRUE(feq(d[1], 0.0f));
		EXPECT_TRUE(feq(d[5], 1.0f));
	}

	oakcore_samplebuffer_silence(si);
	for (int ch = 0; ch < 2; ch++) {
		const float *d = oakcore_samplebuffer_data(si, ch);
		for (int i = 0; i < 10; i++) {
			EXPECT_TRUE(feq(d[i], 0.0f));
		}
	}
	oakcore_samplebuffer_free(si);
}

TEST_F(OakcoreSampleBufferTest, Reverse)
{
	OakSampleBuffer *rv = oakcore_samplebuffer_create_samples(stereo_, 5);
	for (int i = 0; i < 5; i++) {
		oakcore_samplebuffer_data(rv, 0)[i] = (float)i;
		oakcore_samplebuffer_data(rv, 1)[i] = (float)(10 * i);
	}
	oakcore_samplebuffer_reverse(rv);
	for (int i = 0; i < 5; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(rv, 0)[i], (float)(4 - i)));
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(rv, 1)[i], (float)(10 * (4 - i))));
	}
	oakcore_samplebuffer_free(rv);
}

TEST_F(OakcoreSampleBufferTest, Speed)
{
	OakSampleBuffer *sp = oakcore_samplebuffer_create_samples(stereo_, 100);
	for (int i = 0; i < 100; i++) {
		oakcore_samplebuffer_data(sp, 0)[i] = (float)i;
	}
	oakcore_samplebuffer_speed(sp, 2.0);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(sp), 50);
	for (int i = 0; i < 50; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(sp, 0)[i], (float)(2 * i)));
	}
	oakcore_samplebuffer_free(sp);
}

TEST_F(OakcoreSampleBufferTest, FastSet)
{
	OakSampleBuffer *fa = oakcore_samplebuffer_create_samples(stereo_, 10);
	OakSampleBuffer *fb = oakcore_samplebuffer_create_samples(stereo_, 10);
	for (int i = 0; i < 10; i++) {
		oakcore_samplebuffer_data(fa, 0)[i] = (float)i;
		oakcore_samplebuffer_data(fa, 1)[i] = (float)(100 + i);
	}

	oakcore_samplebuffer_fast_set(fb, fa, 1, 0);
	for (int i = 0; i < 10; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(fb, 1)[i], (float)i));
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(fb, 0)[i], 0.0f));
	}

	oakcore_samplebuffer_fast_set(fb, fa, 0, -1);
	for (int i = 0; i < 10; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(fb, 0)[i], (float)i));
	}
	oakcore_samplebuffer_free(fa);
	oakcore_samplebuffer_free(fb);
}

TEST_F(OakcoreSampleBufferTest, RipChannel)
{
	OakSampleBuffer *buf = oakcore_samplebuffer_create_samples(stereo_, 100);
	for (int i = 0; i < 100; i++) {
		oakcore_samplebuffer_data(buf, 1)[i] = (float)(i * 10);
	}

	OakSampleBuffer *rip = oakcore_samplebuffer_rip_channel(buf, 1);
	EXPECT_EQ(oakcore_samplebuffer_is_allocated(rip), 1);
	EXPECT_EQ(oakcore_samplebuffer_channel_count(rip), 1);
	EXPECT_EQ(oakcore_samplebuffer_sample_count(rip), 100);
	for (int i = 0; i < 100; i++) {
		EXPECT_TRUE(feq(oakcore_samplebuffer_data(rip, 0)[i], (float)(i * 10)));
	}
	OakAudioParams *rip_params = oakcore_samplebuffer_audio_params(rip);
	EXPECT_EQ(oakcore_audioparams_sample_rate(rip_params), SAMPLE_RATE);
	EXPECT_EQ(oakcore_audioparams_channel_count(rip_params), 1);
	oakcore_audioparams_free(rip_params);
	oakcore_samplebuffer_free(rip);
	oakcore_samplebuffer_free(buf);
}

TEST_F(OakcoreSampleBufferTest, RipChannelVector)
{
	OakSampleBuffer *buf = oakcore_samplebuffer_create_samples(stereo_, 100);
	for (int i = 0; i < 100; i++) {
		oakcore_samplebuffer_data(buf, 1)[i] = (float)(i * 10);
	}

	EXPECT_EQ(oakcore_samplebuffer_rip_channel_vector(buf, 1, nullptr, 0), 100);

	float partial[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	EXPECT_EQ(oakcore_samplebuffer_rip_channel_vector(buf, 1, partial, 4), 100);
	for (int k = 0; k < 4; k++) {
		EXPECT_TRUE(feq(partial[k], (float)(k * 10)));
	}

	float full[100];
	EXPECT_EQ(oakcore_samplebuffer_rip_channel_vector(buf, 1, full, 100), 100);
	for (int i = 0; i < 100; i++) {
		EXPECT_TRUE(feq(full[i], (float)(i * 10)));
	}
	oakcore_samplebuffer_free(buf);
}
