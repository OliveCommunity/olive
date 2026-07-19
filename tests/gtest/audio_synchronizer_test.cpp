#include <gtest/gtest.h>

#include "audio/audiosynchronizer.h"

TEST(AudioSynchronizer, PlacesCandidateBySourceStartTime)
{
	olive::AudioSynchronizer::SourceClip reference;
	reference.source_start_time = olive::core::Rational(100);
	reference.has_source_start_time = true;

	olive::AudioSynchronizer::SourceClip candidate;
	candidate.source_start_time = olive::core::Rational(112);
	candidate.has_source_start_time = true;

	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_source_time(reference, candidate,
													olive::core::Rational(10));

	ASSERT_TRUE(placement.valid);
	EXPECT_EQ(placement.timeline_in, olive::core::Rational(22));
}

TEST(AudioSynchronizer, AccountsForMediaInWhenPlacingBySourceTime)
{
	olive::AudioSynchronizer::SourceClip reference;
	reference.source_start_time = olive::core::Rational(100);
	reference.media_in = olive::core::Rational(2);
	reference.has_source_start_time = true;

	olive::AudioSynchronizer::SourceClip candidate;
	candidate.source_start_time = olive::core::Rational(100);
	candidate.media_in = olive::core::Rational(5);
	candidate.has_source_start_time = true;

	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_source_time(reference, candidate,
													olive::core::Rational(20));

	ASSERT_TRUE(placement.valid);
	EXPECT_EQ(placement.timeline_in, olive::core::Rational(23));
}

TEST(AudioSynchronizer, RejectsMissingSourceStartTime)
{
	olive::AudioSynchronizer::SourceClip reference;
	reference.source_start_time = olive::core::Rational(100);
	reference.has_source_start_time = true;

	olive::AudioSynchronizer::SourceClip candidate;

	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_source_time(reference, candidate,
													olive::core::Rational(10));

	EXPECT_FALSE(placement.valid);
}

TEST(AudioSynchronizer, PlacesCandidateByWaveformOffset)
{
	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_waveform_offset(
			olive::core::Rational(10), 24000, 48000);

	ASSERT_TRUE(placement.valid);
	EXPECT_EQ(placement.timeline_in, olive::core::Rational(21, 2));
}

TEST(AudioSynchronizer, SupportsCandidateLeadByWaveformOffset)
{
	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_waveform_offset(
			olive::core::Rational(10), -48000, 48000);

	ASSERT_TRUE(placement.valid);
	EXPECT_EQ(placement.timeline_in, olive::core::Rational(9));
}
