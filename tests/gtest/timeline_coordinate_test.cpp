#include <gtest/gtest.h>

#include "timeline/timelinecoordinate.h"

TEST(TimelineCoordinate, DefaultAndSetters)
{
	olive::TimelineCoordinate coord;
	EXPECT_EQ(coord.get_track().type(), olive::Track::k_none);
	EXPECT_EQ(coord.get_track().index(), 0);

	const olive::core::Rational frame(10, 1);
	olive::Track::Reference ref(olive::Track::k_video, 2);

	coord.set_frame(frame);
	coord.set_track(ref);

	EXPECT_EQ(coord.get_frame(), frame);
	EXPECT_EQ(coord.get_track(), ref);
}

TEST(TimelineCoordinate, Constructors)
{
	const olive::core::Rational frame(5, 1);
	olive::Track::Reference ref(olive::Track::k_audio, 1);

	olive::TimelineCoordinate with_ref(frame, ref);
	EXPECT_EQ(with_ref.get_frame(), frame);
	EXPECT_EQ(with_ref.get_track(), ref);

	olive::TimelineCoordinate with_type(frame, olive::Track::k_subtitle, 3);
	EXPECT_EQ(with_type.get_frame(), frame);
	EXPECT_EQ(with_type.get_track().type(), olive::Track::k_subtitle);
	EXPECT_EQ(with_type.get_track().index(), 3);
}

TEST(TimelineCoordinate, CopyAndAssignment)
{
	const olive::core::Rational frame(7, 1);
	olive::Track::Reference ref(olive::Track::k_video, 4);

	olive::TimelineCoordinate original(frame, ref);
	olive::TimelineCoordinate copy(original);
	EXPECT_EQ(copy.get_frame(), frame);
	EXPECT_EQ(copy.get_track(), ref);

	olive::TimelineCoordinate assigned;
	assigned = original;
	EXPECT_EQ(assigned.get_frame(), frame);
	EXPECT_EQ(assigned.get_track(), ref);
}

TEST(TimelineCoordinate, Equality)
{
	// TimelineCoordinate provides no operator== of its own; equality is
	// observable through the real operators of its components (Rational and
	// Track::Reference)
	const olive::TimelineCoordinate a(
		olive::core::Rational(5, 1),
		olive::Track::Reference(olive::Track::k_video, 1));

	// Distinct objects with identical frame and track compare equal in both
	// components
	const olive::TimelineCoordinate b(
		olive::core::Rational(5, 1),
		olive::Track::Reference(olive::Track::k_video, 1));
	EXPECT_TRUE(a.get_frame() == b.get_frame());
	EXPECT_TRUE(a.get_track() == b.get_track());
	EXPECT_FALSE(a.get_frame() != b.get_frame());
	EXPECT_FALSE(a.get_track() != b.get_track());

	// A different frame breaks frame equality while the track stays equal
	const olive::TimelineCoordinate c(
		olive::core::Rational(6, 1),
		olive::Track::Reference(olive::Track::k_video, 1));
	EXPECT_FALSE(a.get_frame() == c.get_frame());
	EXPECT_TRUE(a.get_frame() != c.get_frame());
	EXPECT_TRUE(a.get_track() == c.get_track());

	// A different track type or index breaks track equality while the frame
	// stays equal
	const olive::TimelineCoordinate d(
		olive::core::Rational(5, 1),
		olive::Track::Reference(olive::Track::k_audio, 1));
	const olive::TimelineCoordinate e(
		olive::core::Rational(5, 1),
		olive::Track::Reference(olive::Track::k_video, 2));
	EXPECT_TRUE(a.get_track() != d.get_track());
	EXPECT_FALSE(a.get_track() == d.get_track());
	EXPECT_TRUE(a.get_track() != e.get_track());
	EXPECT_TRUE(a.get_frame() == d.get_frame());
	EXPECT_TRUE(a.get_frame() == e.get_frame());

	// Mutating a copy breaks equality with the original
	olive::TimelineCoordinate mutated = a;
	mutated.set_frame(olive::core::Rational(7, 1));
	EXPECT_TRUE(mutated.get_frame() != a.get_frame());
	mutated.set_frame(olive::core::Rational(5, 1));
	mutated.set_track(olive::Track::Reference(olive::Track::k_subtitle, 0));
	EXPECT_TRUE(mutated.get_track() != a.get_track());
}
