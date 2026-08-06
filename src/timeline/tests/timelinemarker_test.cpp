/***

  Oak Video Editor - Non-Linear Video Editor
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

#include <gtest/gtest.h>

#include "timelinemarker.h"

using namespace olive;

TEST(TimelineMarker, AddKeepsSortedByTime)
{
	TimelineMarkerList list;

	list.add_marker(
		std::make_unique<TimelineMarker>(0, TimeRange(Rational(10), Rational(20))));
	list.add_marker(
		std::make_unique<TimelineMarker>(1, TimeRange(Rational(0), Rational(5))));

	ASSERT_EQ(list.size(), 2);
	EXPECT_EQ(list.at(0)->time().in(), Rational(0));
	EXPECT_EQ(list.at(1)->time().in(), Rational(10));
}

TEST(TimelineMarker, SetTimeResorts)
{
	TimelineMarkerList list;

	auto *late = new TimelineMarker(0, TimeRange(Rational(10), Rational(20)));
	list.add_marker(std::unique_ptr<TimelineMarker>(late));
	list.add_marker(
		std::make_unique<TimelineMarker>(1, TimeRange(Rational(5), Rational(6))));

	late->set_time(TimeRange(Rational(1), Rational(2)));

	EXPECT_EQ(list.at(0)->time().in(), Rational(1));
	EXPECT_EQ(list.at(1)->time().in(), Rational(5));
}

TEST(TimelineMarker, RemoveMarkerReturnsOwnership)
{
	TimelineMarkerList list;

	auto *raw = new TimelineMarker(0, TimeRange(Rational(0), Rational(5)));
	list.add_marker(std::unique_ptr<TimelineMarker>(raw));

	auto removed = list.remove_marker(raw);
	ASSERT_TRUE(removed != nullptr);
	EXPECT_EQ(removed.get(), raw);
	EXPECT_EQ(list.size(), 0);
}

TEST(TimelineMarker, RemoveMissingMarkerReturnsNull)
{
	TimelineMarkerList list;
	TimelineMarker not_in_list;

	EXPECT_EQ(list.remove_marker(&not_in_list), nullptr);
}

TEST(TimelineMarker, GetMarkerAtTime)
{
	TimelineMarkerList list;

	list.add_marker(
		std::make_unique<TimelineMarker>(0, TimeRange(Rational(10), Rational(20))));

	EXPECT_NE(list.get_marker_at_time(Rational(10)), nullptr);
	EXPECT_EQ(list.get_marker_at_time(Rational(11)), nullptr);
}

TEST(TimelineMarker, MarkerAddCommandUndoRedo)
{
	TimelineMarkerList list;

	MarkerAddCommand cmd(&list, TimeRange(Rational(0), Rational(5)), "m", 2);

	cmd.redo_now();
	ASSERT_EQ(list.size(), 1);
	EXPECT_EQ(list.at(0)->name(), "m");
	EXPECT_EQ(list.at(0)->color(), 2);

	cmd.undo_now();
	EXPECT_EQ(list.size(), 0);

	cmd.redo_now();
	EXPECT_EQ(list.size(), 1);
}

TEST(TimelineMarker, MarkerRemoveCommandUndoRedo)
{
	TimelineMarkerList list;

	auto *raw = new TimelineMarker(0, TimeRange(Rational(0), Rational(5)));
	list.add_marker(std::unique_ptr<TimelineMarker>(raw));

	MarkerRemoveCommand cmd(raw, &list);

	cmd.redo_now();
	EXPECT_EQ(list.size(), 0);

	cmd.undo_now();
	ASSERT_EQ(list.size(), 1);
	EXPECT_EQ(list.at(0), raw);
}

TEST(TimelineMarker, ChangeCommandsUndoRedo)
{
	TimelineMarker marker(1, TimeRange(Rational(0), Rational(5)), "old");

	MarkerChangeColorCommand color_cmd(&marker, 3);
	color_cmd.redo_now();
	EXPECT_EQ(marker.color(), 3);
	color_cmd.undo_now();
	EXPECT_EQ(marker.color(), 1);

	MarkerChangeNameCommand name_cmd(&marker, "new");
	name_cmd.redo_now();
	EXPECT_EQ(marker.name(), "new");
	name_cmd.undo_now();
	EXPECT_EQ(marker.name(), "old");

	MarkerChangeTimeCommand time_cmd(&marker, TimeRange(Rational(7), Rational(9)));
	time_cmd.redo_now();
	EXPECT_EQ(marker.time().in(), Rational(7));
	time_cmd.undo_now();
	EXPECT_EQ(marker.time().in(), Rational(0));
}

TEST(TimelineMarker, XmlRoundTrip)
{
	TimelineMarkerList list;
	list.add_marker(std::make_unique<TimelineMarker>(
		3, TimeRange(Rational(1, 2), Rational(3, 4)), "hello"));

	XmlStreamWriter writer;
	writer.write_start_element("markers");
	list.save(&writer);
	writer.write_end_element();

	std::string xml = writer.output();

	XmlStreamReader reader(xml);
	TimelineMarkerList loaded;
	ASSERT_TRUE(xml_read_next_start_element(&reader));
	ASSERT_TRUE(loaded.load(&reader));

	ASSERT_EQ(loaded.size(), 1);
	EXPECT_EQ(loaded.at(0)->name(), "hello");
	EXPECT_EQ(loaded.at(0)->color(), 3);
	EXPECT_EQ(loaded.at(0)->time().in(), Rational(1, 2));
	EXPECT_EQ(loaded.at(0)->time().out(), Rational(3, 4));
}
