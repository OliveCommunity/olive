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

// Pure C ABI test for the NodeValue static facade methods
// (oakengine_node_value_keyframe_track_count / _pretty_type_name /
// _split_to_tracks / _combine_tracks). Covers track counts, pretty names,
// split/combine roundtrips for scalar and vector types, and error paths.
// No engine init required: these wrap pure NodeValue statics.

#include <assert.h>
#include <gtest/gtest.h>
#include <math.h>
#include <string.h>

#include "oakengine/node.h"

static void test_track_count(void)
{
	EXPECT_TRUE(oakengine_node_value_keyframe_track_count(OAK_NODE_VALUE_INT) ==
		   1);
	EXPECT_TRUE(oakengine_node_value_keyframe_track_count(OAK_NODE_VALUE_FLOAT) ==
		   1);
	EXPECT_TRUE(oakengine_node_value_keyframe_track_count(OAK_NODE_VALUE_VEC2) ==
		   2);
	EXPECT_TRUE(oakengine_node_value_keyframe_track_count(OAK_NODE_VALUE_VEC3) ==
		   3);
	EXPECT_TRUE(oakengine_node_value_keyframe_track_count(OAK_NODE_VALUE_VEC4) ==
		   4);
}

static void test_pretty_name(void)
{
	char buf[64];

	EXPECT_TRUE(oakengine_node_value_pretty_type_name(OAK_NODE_VALUE_INT, buf,
												 sizeof(buf)) > 0);
	EXPECT_TRUE(buf[0] != '\0');

	/* two-phase: query length first */
	const int len =
		oakengine_node_value_pretty_type_name(OAK_NODE_VALUE_FLOAT, nullptr,
											  0);
	EXPECT_TRUE(len > 0);

	/* unknown type reports -1 */
	EXPECT_TRUE(oakengine_node_value_pretty_type_name(9999, buf, sizeof(buf)) ==
		   -1);
}

static void test_split_combine_vec3(void)
{
	oak_node_value normal = {0};
	normal.type = OAK_NODE_VALUE_VEC3;
	normal.f[0] = 1.0;
	normal.f[1] = 2.0;
	normal.f[2] = 3.0;

	oak_node_value tracks[3] = {{0}};
	EXPECT_TRUE(oakengine_node_value_split_to_tracks(OAK_NODE_VALUE_VEC3, &normal,
												tracks, 3) == OAKENGINE_OK);
	EXPECT_TRUE(tracks[0].f[0] == 1.0);
	EXPECT_TRUE(tracks[1].f[0] == 2.0);
	EXPECT_TRUE(tracks[2].f[0] == 3.0);

	oak_node_value back = {0};
	EXPECT_TRUE(oakengine_node_value_combine_tracks(OAK_NODE_VALUE_VEC3, tracks,
											   3, &back) == OAKENGINE_OK);
	EXPECT_TRUE(back.type == OAK_NODE_VALUE_VEC3);
	EXPECT_TRUE(back.f[0] == 1.0 && back.f[1] == 2.0 && back.f[2] == 3.0);
}

static void test_split_combine_int(void)
{
	oak_node_value normal = {0};
	normal.type = OAK_NODE_VALUE_INT;
	normal.num = 42;

	oak_node_value track = {0};
	EXPECT_TRUE(oakengine_node_value_split_to_tracks(OAK_NODE_VALUE_INT, &normal,
												&track, 1) == OAKENGINE_OK);
	/* scalar fields must survive the roundtrip (num, not only f[0]) */
	EXPECT_TRUE(track.type == OAK_NODE_VALUE_INT);
	EXPECT_TRUE(track.num == 42);

	oak_node_value back = {0};
	EXPECT_TRUE(oakengine_node_value_combine_tracks(OAK_NODE_VALUE_INT, &track, 1,
											   &back) == OAKENGINE_OK);
	EXPECT_TRUE(back.type == OAK_NODE_VALUE_INT);
	EXPECT_TRUE(back.num == 42);
}

static void test_split_combine_rational(void)
{
	oak_node_value normal = {0};
	normal.type = OAK_NODE_VALUE_RATIONAL;
	normal.num = 30000;
	normal.den = 1001;

	oak_node_value track = {0};
	EXPECT_TRUE(oakengine_node_value_split_to_tracks(OAK_NODE_VALUE_RATIONAL,
												&normal, &track,
												1) == OAKENGINE_OK);
	EXPECT_TRUE(track.type == OAK_NODE_VALUE_RATIONAL);
	EXPECT_TRUE(track.num == 30000 && track.den == 1001);

	oak_node_value back = {0};
	EXPECT_TRUE(oakengine_node_value_combine_tracks(OAK_NODE_VALUE_RATIONAL,
											   &track, 1,
											   &back) == OAKENGINE_OK);
	EXPECT_TRUE(back.num == 30000 && back.den == 1001);
}

static void test_error_paths(void)
{
	oak_node_value v = {0};

	EXPECT_TRUE(oakengine_node_value_split_to_tracks(OAK_NODE_VALUE_VEC3, nullptr,
												&v, 1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_value_split_to_tracks(OAK_NODE_VALUE_VEC3, &v,
												nullptr,
												1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_value_split_to_tracks(OAK_NODE_VALUE_VEC3, &v, &v,
												0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_value_combine_tracks(OAK_NODE_VALUE_VEC3, nullptr,
											   1, &v) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_value_combine_tracks(OAK_NODE_VALUE_VEC3, &v, 1,
											   nullptr) ==
		   OAKENGINE_E_INVALID);
}

TEST(OakEngineNodevalue, Main)
{
	test_track_count();
	test_pretty_name();
	test_split_combine_vec3();
	test_split_combine_int();
	test_split_combine_rational();
	test_error_paths();
}
