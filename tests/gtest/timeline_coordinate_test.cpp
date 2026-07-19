#include <gtest/gtest.h>

#include "timeline/timelinecoordinate.h"

TEST(TimelineCoordinate, DefaultAndSetters)
{
	olive::TimelineCoordinate coord;
	EXPECT_EQ(coord.GetTrack().type(), olive::Track::kNone);
	EXPECT_EQ(coord.GetTrack().index(), 0);

	const olive::core::rational frame(10, 1);
	olive::Track::Reference ref(olive::Track::kVideo, 2);

	coord.SetFrame(frame);
	coord.SetTrack(ref);

	EXPECT_EQ(coord.GetFrame(), frame);
	EXPECT_EQ(coord.GetTrack(), ref);
}

TEST(TimelineCoordinate, Constructors)
{
	const olive::core::rational frame(5, 1);
	olive::Track::Reference ref(olive::Track::kAudio, 1);

	olive::TimelineCoordinate with_ref(frame, ref);
	EXPECT_EQ(with_ref.GetFrame(), frame);
	EXPECT_EQ(with_ref.GetTrack(), ref);

	olive::TimelineCoordinate with_type(frame, olive::Track::kSubtitle, 3);
	EXPECT_EQ(with_type.GetFrame(), frame);
	EXPECT_EQ(with_type.GetTrack().type(), olive::Track::kSubtitle);
	EXPECT_EQ(with_type.GetTrack().index(), 3);
}

TEST(TimelineCoordinate, CopyAndAssignment)
{
	const olive::core::rational frame(7, 1);
	olive::Track::Reference ref(olive::Track::kVideo, 4);

	olive::TimelineCoordinate original(frame, ref);
	olive::TimelineCoordinate copy(original);
	EXPECT_EQ(copy.GetFrame(), frame);
	EXPECT_EQ(copy.GetTrack(), ref);

	olive::TimelineCoordinate assigned;
	assigned = original;
	EXPECT_EQ(assigned.GetFrame(), frame);
	EXPECT_EQ(assigned.GetTrack(), ref);
}

TEST(TimelineCoordinate, Equality)
{
	// TimelineCoordinate provides no operator== of its own; equality is
	// observable through the real operators of its components (rational and
	// Track::Reference)
	const olive::TimelineCoordinate a(
		olive::core::rational(5, 1),
		olive::Track::Reference(olive::Track::kVideo, 1));

	// Distinct objects with identical frame and track compare equal in both
	// components
	const olive::TimelineCoordinate b(
		olive::core::rational(5, 1),
		olive::Track::Reference(olive::Track::kVideo, 1));
	EXPECT_TRUE(a.GetFrame() == b.GetFrame());
	EXPECT_TRUE(a.GetTrack() == b.GetTrack());
	EXPECT_FALSE(a.GetFrame() != b.GetFrame());
	EXPECT_FALSE(a.GetTrack() != b.GetTrack());

	// A different frame breaks frame equality while the track stays equal
	const olive::TimelineCoordinate c(
		olive::core::rational(6, 1),
		olive::Track::Reference(olive::Track::kVideo, 1));
	EXPECT_FALSE(a.GetFrame() == c.GetFrame());
	EXPECT_TRUE(a.GetFrame() != c.GetFrame());
	EXPECT_TRUE(a.GetTrack() == c.GetTrack());

	// A different track type or index breaks track equality while the frame
	// stays equal
	const olive::TimelineCoordinate d(
		olive::core::rational(5, 1),
		olive::Track::Reference(olive::Track::kAudio, 1));
	const olive::TimelineCoordinate e(
		olive::core::rational(5, 1),
		olive::Track::Reference(olive::Track::kVideo, 2));
	EXPECT_TRUE(a.GetTrack() != d.GetTrack());
	EXPECT_FALSE(a.GetTrack() == d.GetTrack());
	EXPECT_TRUE(a.GetTrack() != e.GetTrack());
	EXPECT_TRUE(a.GetFrame() == d.GetFrame());
	EXPECT_TRUE(a.GetFrame() == e.GetFrame());

	// Mutating a copy breaks equality with the original
	olive::TimelineCoordinate mutated = a;
	mutated.SetFrame(olive::core::rational(7, 1));
	EXPECT_TRUE(mutated.GetFrame() != a.GetFrame());
	mutated.SetFrame(olive::core::rational(5, 1));
	mutated.SetTrack(olive::Track::Reference(olive::Track::kSubtitle, 0));
	EXPECT_TRUE(mutated.GetTrack() != a.GetTrack());
}
