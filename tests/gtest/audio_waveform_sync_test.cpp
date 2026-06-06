#include <gtest/gtest.h>

#include "audio/audiowaveformsync.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/render/sampleformat.h"

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace {

olive::core::AudioParams MakeMonoParams()
{
	return olive::core::AudioParams(48000, AV_CH_LAYOUT_MONO,
									olive::core::SampleFormat::F32P);
}

olive::core::SampleBuffer MakeBuffer(const QVector<float> &values)
{
	olive::core::SampleBuffer samples(MakeMonoParams(),
									  static_cast<size_t>(values.size()));
	float *data = samples.data(0);
	for (int i = 0; i < values.size(); i++) {
		data[i] = values.at(i);
	}
	return samples;
}

}

TEST(AudioWaveformSync, ExtractsRmsEnvelope)
{
	olive::core::SampleBuffer samples =
		MakeBuffer({ 1.0f, -1.0f, 0.5f, -0.5f, 0.0f, 0.0f });

	const QVector<double> envelope =
		olive::AudioWaveformSync::ExtractRmsEnvelope(samples, 2);

	ASSERT_EQ(envelope.size(), 3);
	EXPECT_NEAR(envelope.at(0), 1.0, 0.0001);
	EXPECT_NEAR(envelope.at(1), 0.5, 0.0001);
	EXPECT_NEAR(envelope.at(2), 0.0, 0.0001);
}

TEST(AudioWaveformSync, EstimatesCandidateLag)
{
	const QVector<float> reference_values = {
		0.0f, 0.0f, 0.8f, 0.8f, 0.1f, 0.1f, 0.6f, 0.6f, 0.0f, 0.0f
	};
	const QVector<float> candidate_values = {
		0.0f, 0.0f, 0.0f, 0.0f, 0.8f, 0.8f, 0.1f,
		0.1f, 0.6f, 0.6f, 0.0f, 0.0f
	};

	const olive::AudioWaveformSync::OffsetResult result =
		olive::AudioWaveformSync::EstimateOffset(
			MakeBuffer(reference_values), MakeBuffer(candidate_values), 2, 8);

	ASSERT_TRUE(result.valid);
	EXPECT_EQ(result.offset_samples, 2);
	EXPECT_GT(result.confidence, 0.99);
}

TEST(AudioWaveformSync, EstimatesCandidateLead)
{
	const QVector<float> reference_values = {
		0.0f, 0.0f, 0.0f, 0.0f, 0.9f, 0.9f, 0.3f,
		0.3f, 0.7f, 0.7f, 0.0f, 0.0f
	};
	const QVector<float> candidate_values = {
		0.9f, 0.9f, 0.3f, 0.3f, 0.7f, 0.7f, 0.0f, 0.0f
	};

	const olive::AudioWaveformSync::OffsetResult result =
		olive::AudioWaveformSync::EstimateOffset(
			MakeBuffer(reference_values), MakeBuffer(candidate_values), 2, 4);

	ASSERT_TRUE(result.valid);
	EXPECT_EQ(result.offset_samples, -4);
	EXPECT_GT(result.confidence, 0.99);
}

TEST(AudioWaveformSync, RejectsSilence)
{
	olive::core::SampleBuffer reference(MakeMonoParams(), size_t(16));
	olive::core::SampleBuffer candidate(MakeMonoParams(), size_t(16));
	reference.silence();
	candidate.silence();

	const olive::AudioWaveformSync::OffsetResult result =
		olive::AudioWaveformSync::EstimateOffset(reference, candidate, 4, 16);

	EXPECT_FALSE(result.valid);
}
