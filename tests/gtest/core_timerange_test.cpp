#include <gtest/gtest.h>

#include "olive/core/util/timerange.h"

using namespace olive::core;

TEST(CoreTimeRange, ConstructAndAccess)
{
	TimeRange r(Rational(1), Rational(5));
	EXPECT_EQ(r.in(), Rational(1));
	EXPECT_EQ(r.out(), Rational(5));
	EXPECT_EQ(r.length(), Rational(4));
}

TEST(CoreTimeRange, NormalizationSwapsReversedBounds)
{
	TimeRange r(Rational(5), Rational(1));
	EXPECT_EQ(r.in(), Rational(1));
	EXPECT_EQ(r.out(), Rational(5));
}

TEST(CoreTimeRange, SettersNormalize)
{
	TimeRange r(Rational(0), Rational(10));
	r.set_in(Rational(15));
	EXPECT_EQ(r.in(), Rational(10));
	EXPECT_EQ(r.out(), Rational(15));

	r.set_out(Rational(2));
	EXPECT_EQ(r.in(), Rational(2));
	EXPECT_EQ(r.out(), Rational(10));
}

TEST(CoreTimeRange, ContainsRational)
{
	TimeRange r(Rational(0), Rational(10));
	EXPECT_TRUE(r.contains(Rational(5)));
	EXPECT_FALSE(r.contains(Rational(10)));
	EXPECT_FALSE(r.contains(Rational(-1)));
}

TEST(CoreTimeRange, ContainsRange)
{
	TimeRange outer(Rational(0), Rational(10));
	TimeRange inner(Rational(2), Rational(8));
	TimeRange partial(Rational(5), Rational(15));

	EXPECT_TRUE(outer.contains(inner));
	EXPECT_FALSE(outer.contains(partial));
}

TEST(CoreTimeRange, OverlapsWith)
{
	TimeRange a(Rational(0), Rational(10));
	TimeRange b(Rational(5), Rational(15));
	TimeRange c(Rational(10), Rational(20));

	EXPECT_TRUE(a.overlaps_with(b));
	// By default bounds are inclusive, so [0,10] and [10,20] touch and overlap
	EXPECT_TRUE(a.overlaps_with(c));
	EXPECT_FALSE(a.overlaps_with(c, false, false));
}

TEST(CoreTimeRange, CombineAndIntersect)
{
	TimeRange a(Rational(0), Rational(10));
	TimeRange b(Rational(5), Rational(15));

	TimeRange combined = a.combined(b);
	EXPECT_EQ(combined.in(), Rational(0));
	EXPECT_EQ(combined.out(), Rational(15));

	TimeRange intersect = a.intersected(b);
	EXPECT_EQ(intersect.in(), Rational(5));
	EXPECT_EQ(intersect.out(), Rational(10));
}

TEST(CoreTimeRange, Arithmetic)
{
	TimeRange r(Rational(0), Rational(10));
	TimeRange shifted = r + Rational(5);
	EXPECT_EQ(shifted.in(), Rational(5));
	EXPECT_EQ(shifted.out(), Rational(15));

	shifted -= Rational(3);
	EXPECT_EQ(shifted.in(), Rational(2));
	EXPECT_EQ(shifted.out(), Rational(12));
}

TEST(CoreTimeRange, Split)
{
	TimeRange r(Rational(0), Rational(10));
	auto pieces = r.split(3);
	ASSERT_EQ(pieces.size(), 4u);
	EXPECT_EQ(pieces.front().in(), Rational(0));
}

TEST(CoreTimeRangeList, InsertMergesOverlapping)
{
	TimeRangeList list;
	list.insert(TimeRange(Rational(0), Rational(5)));
	list.insert(TimeRange(Rational(3), Rational(8)));
	list.insert(TimeRange(Rational(10), Rational(12)));

	EXPECT_EQ(list.size(), 2);
	EXPECT_EQ(list.first().in(), Rational(0));
	EXPECT_EQ(list.first().out(), Rational(8));
}

TEST(CoreTimeRangeList, RemoveSplitsRange)
{
	TimeRangeList list;
	list.insert(TimeRange(Rational(0), Rational(10)));
	list.remove(TimeRange(Rational(3), Rational(7)));

	EXPECT_EQ(list.size(), 2);
	EXPECT_EQ(list.first().out(), Rational(3));
	EXPECT_EQ(list.last().in(), Rational(7));
}

TEST(CoreTimeRangeList, Shift)
{
	TimeRangeList list;
	list.insert(TimeRange(Rational(0), Rational(5)));
	list.shift(Rational(10));

	EXPECT_EQ(list.first().in(), Rational(10));
	EXPECT_EQ(list.first().out(), Rational(15));
}

TEST(CoreTimeRangeList, TrimInAndOut)
{
	TimeRangeList list;
	list.insert(TimeRange(Rational(10), Rational(20)));
	list.trim_in(Rational(5));
	EXPECT_EQ(list.first().in(), Rational(15));
	EXPECT_EQ(list.first().out(), Rational(20));

	list.trim_out(Rational(-5));
	// set_out(out + diff) = 20 + (-5) = 15
	EXPECT_EQ(list.first().out(), Rational(15));
}

TEST(CoreTimeRangeList, Intersects)
{
	TimeRangeList list;
	list.insert(TimeRange(Rational(0), Rational(10)));
	list.insert(TimeRange(Rational(20), Rational(30)));

	TimeRangeList result =
		list.intersects(TimeRange(Rational(5), Rational(25)));
	EXPECT_EQ(result.size(), 2);
	EXPECT_EQ(result.first().in(), Rational(5));
	EXPECT_EQ(result.first().out(), Rational(10));
}

TEST(CoreTimeRangeListFrameIterator, IteratesFrames)
{
	TimeRangeList list;
	// 5 seconds at 25fps = 125 frames
	list.insert(TimeRange(Rational(0), Rational(5)));
	TimeRangeListFrameIterator it(list, Rational(1, 25));

	Rational out;
	int count = 0;
	while (it.get_next(&out)) {
		count++;
	}

	EXPECT_EQ(count, 125);
	EXPECT_EQ(it.size(), 125);
}

TEST(CoreTimeRangeListFrameIterator, HasNext)
{
	TimeRangeList list;
	list.insert(TimeRange(Rational(0), Rational(1)));
	TimeRangeListFrameIterator it(list, Rational(1, 25));

	EXPECT_TRUE(it.has_next());
	Rational out;
	while (it.get_next(&out)) {
	}
	EXPECT_FALSE(it.has_next());
}
