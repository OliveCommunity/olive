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

// Pure C ABI test for the liboakengine keyframe (parameter animation)
// facade: is-keyframed, add/at/easing/remove/clear on track 0, across
// FLOAT, RATIONAL and COLOR (first-component) parameters, with the full
// undo/redo chain. No GL required.

#include <assert.h>
#include <gtest/gtest.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "oakengine/init.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/timeline.h"

#ifndef OAK_TEST_SOURCE_DIR
#define OAK_TEST_SOURCE_DIR "."
#endif

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	EXPECT_TRUE(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_keyframe_test_%lu",
			 base, (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_keyframe_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static oak_node_value float_value(double v)
{
	oak_node_value out;
	memset(&out, 0, sizeof(out));
	out.type = OAK_NODE_VALUE_FLOAT;
	out.f[0] = v;
	return out;
}

static void test_float_lifecycle(OakEngineProject *project,
								 OakEngineNode *opacity)
{
	char err[256];

	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 0);

	// Missing input ids and missing keyframes report errors.
	oak_node_value v = float_value(0.5);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "no_such_input", 0, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_last_error(err, sizeof(err)) > 0);
	EXPECT_TRUE(oakengine_node_keyframe_add(NULL, "opacity_in", 0, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 0, NULL, 0, 0,
									   0, 0, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 0, &v, 9, 0, 0,
									   0, 0) == OAKENGINE_E_INVALID);

	// Three linear keys at 0, 15, 30.
	v = float_value(0.0);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 0, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 1);
	v = float_value(0.5);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 15, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_OK);
	v = float_value(1.0);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 30, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);

	// A key at an occupied time is refused.
	v = float_value(0.25);
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 15, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_E_STATE);

	// Read them back: ordered, correct times and values, linear easing.
	int64_t ts = -1;
	oak_node_value out;
	for (int i = 0; i < 3; i++) {
		EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", i, &ts,
										  &out) == OAKENGINE_OK);
		EXPECT_TRUE(ts == i * 15);
		EXPECT_TRUE(out.type == OAK_NODE_VALUE_FLOAT);
		EXPECT_TRUE(fabs(out.f[0] - i * 0.5) < 1e-9);
		int type = -1;
		float x1 = -1, y1 = -1, x2 = -1, y2 = -1;
		EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", i,
												  &x1, &y1, &x2, &y2,
												  &type) == OAKENGINE_OK);
		EXPECT_TRUE(type == 0); // linear
		EXPECT_TRUE(x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0);
	}
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 3, &ts, &out) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 99,
											  NULL, NULL, NULL, NULL,
											  NULL) == OAKENGINE_E_NOT_FOUND);

	// Undo the adds one by one, then redo them all.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 2);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 1);
}

static void test_easing_and_remove(OakEngineProject *project,
								   OakEngineNode *opacity)
{
	// Bezier easing on the middle key, control points round-trip.
	EXPECT_TRUE(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 15, 1,
											  0.1f, 0.2f, 0.3f,
											  0.4f) == OAKENGINE_OK);
	int type = -1;
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 1); // bezier
	EXPECT_TRUE(fabsf(x1 - 0.1f) < 1e-6f && fabsf(y1 - 0.2f) < 1e-6f);
	EXPECT_TRUE(fabsf(x2 - 0.3f) < 1e-6f && fabsf(y2 - 0.4f) < 1e-6f);

	// Undo restores linear, redo restores bezier.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 1);

	// Hold easing on the first key.
	EXPECT_TRUE(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 0, 2, 0,
											  0, 0, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 0, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 2); // hold
	EXPECT_TRUE(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 15, 9, 0,
											  0, 0, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 77, 1, 0,
											  0, 0,
											  0) == OAKENGINE_E_NOT_FOUND);

	// Remove the middle key; order compacts.
	EXPECT_TRUE(oakengine_node_keyframe_remove(opacity, "opacity_in", 15) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 2);
	int64_t ts = -1;
	oak_node_value out;
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 30 && fabs(out.f[0] - 1.0) < 1e-9);
	EXPECT_TRUE(oakengine_node_keyframe_remove(opacity, "opacity_in", 15) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 15);

	// Clear all, then undo.
	EXPECT_TRUE(oakengine_node_keyframes_clear(opacity, "opacity_in") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_node_keyframes_clear(opacity, "opacity_in") ==
		   OAKENGINE_OK); // no-op on empty
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
}

static void test_rational_and_color(OakEngineProject *project,
									OakEngineNode *timeremap,
									OakEngineNode *solid)
{
	// RATIONAL value on the time remap node's rational input.
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_RATIONAL;
	v.num = 30;
	v.den = 1;
	EXPECT_TRUE(oakengine_node_keyframe_add(timeremap, "time_in", 0, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(timeremap, "time_in") == 1);
	int64_t ts = -1;
	oak_node_value out;
	EXPECT_TRUE(oakengine_node_keyframe_at(timeremap, "time_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 0);
	EXPECT_TRUE(out.type == OAK_NODE_VALUE_RATIONAL);
	EXPECT_TRUE(out.num == 30 && out.den == 1);

	// A type mismatch is rejected.
	v.type = OAK_NODE_VALUE_FLOAT;
	v.f[0] = 1.5;
	EXPECT_TRUE(oakengine_node_keyframe_add(timeremap, "time_in", 5, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_E_INVALID);

	// COLOR on the Solid generator: track 0 is the red component.
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_COLOR;
	v.f[0] = 0.25;
	v.f[1] = 0.5;
	v.f[2] = 0.75;
	v.f[3] = 1.0;
	EXPECT_TRUE(oakengine_node_keyframe_add(solid, "color_in", 10, &v, 0, 0, 0, 0,
									   0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_at(solid, "color_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 10);
	EXPECT_TRUE(out.type == OAK_NODE_VALUE_COLOR);
	EXPECT_TRUE(fabs(out.f[0] - 0.25) < 1e-6); // first component only

	EXPECT_TRUE(oakengine_node_keyframes_clear(timeremap, "time_in") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframes_clear(solid, "color_in") == OAKENGINE_OK);
}

static void test_panel_paths(OakEngineProject *project,
							 OakEngineNode *opacity, OakEngineNode *solid)
{
	oak_node_value out;

	// Frame time base (1001/30000 with the default sequence).
	int tbn = 0, tbd = 0;
	EXPECT_TRUE(oakengine_node_frame_time_base(opacity, &tbn, &tbd) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(tbn > 0 && tbd > 0);
	EXPECT_TRUE(oakengine_node_frame_time_base(NULL, &tbn, &tbd) ==
		   OAKENGINE_E_INVALID);

	// set_input_at_time on a NON-keyframed input (fresh node) sets the
	// standard value; undo restores it.
	OakEngineNode *op2 = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.opacity");
	EXPECT_TRUE(op2 != NULL);
	oak_node_value v = float_value(0.75);
	EXPECT_TRUE(oakengine_node_set_input_at_time(op2, "opacity_in", -1, 10, 0, &v,
											1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(op2, "opacity_in", &out) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.75) < 1e-9);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(op2, "opacity_in", &out) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.75) > 1e-9);

	// Component type must match the declared type; bad track/id rejected.
	oak_node_value wrong;
	memset(&wrong, 0, sizeof(wrong));
	wrong.type = OAK_NODE_VALUE_INT;
	EXPECT_TRUE(oakengine_node_set_input_at_time(op2, "opacity_in", -1, 0, 0,
											&wrong,
											1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_input_at_time(op2, "opacity_in", -1, 0, 1, &v,
											1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_input_at_time(op2, "nope", -1, 0, 0, &v,
											1) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_set_input_at_time(NULL, "opacity_in", -1, 0, 0, &v,
											1) == OAKENGINE_E_INVALID);

	// `opacity` is keyframed at this point (keys at 0/15/30 left by the
	// earlier tests): the call writes a KEYFRAME at the time
	// (set_value_at_time semantics) -- insert first, then update in place.
	v = float_value(0.9);
	EXPECT_TRUE(oakengine_node_set_input_at_time(opacity, "opacity_in", -1, 20, 0,
											&v, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 4);
	int64_t ts = -1;
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 2, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 20 && fabs(out.f[0] - 0.9) < 1e-9);
	v = float_value(0.8);
	EXPECT_TRUE(oakengine_node_set_input_at_time(opacity, "opacity_in", -1, 20, 0,
											&v, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 4);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 2, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.8) < 1e-9);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);

	// track -1 writes all color components in one command (fresh node, so
	// the standard value itself is written and readable back).
	OakEngineNode *solid2 = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid2 != NULL);
	oak_node_value c;
	memset(&c, 0, sizeof(c));
	c.type = OAK_NODE_VALUE_COLOR;
	c.f[0] = 0.1;
	c.f[1] = 0.2;
	c.f[2] = 0.3;
	c.f[3] = 0.4;
	EXPECT_TRUE(oakengine_node_set_input_at_time(solid2, "color_in", -1, 5, -1, &c,
											0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(solid2, "color_in", &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.1) < 1e-6 && fabs(out.f[1] - 0.2) < 1e-6 &&
		   fabs(out.f[2] - 0.3) < 1e-6 && fabs(out.f[3] - 0.4) < 1e-6);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);

	// String-at-time on the text node; the POD path rejects strings and
	// vice versa.
	OakEngineNode *text = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.text3");
	EXPECT_TRUE(text != NULL);
	EXPECT_TRUE(oakengine_node_set_input_string_at_time(text, "text_in", -1, 0,
												   "hello") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_set_input_string_at_time(text, "nope", -1, 0,
												   "x") == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_set_input_string_at_time(op2, "opacity_in", -1, 0,
												   "x") == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_input_at_time(text, "text_in", -1, 0, 0, &v,
											1) == OAKENGINE_E_INVALID);

	// Array insert/remove on the text node's args_in array.
	EXPECT_TRUE(oakengine_node_array_insert_at(text, "args_in", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_array_insert_at(text, "args_in", 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_array_remove_at(text, "args_in", 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_array_remove_at(text, "args_in", 5) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_array_remove_at(text, "args_in", 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_array_insert_at(text, "args_in", -1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_array_remove_at(NULL, "args_in", 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);

	// keyframes_set_type_many: keys at 0 and 15 to hold in one command.
	const int64_t times[2] = { 0, 15 };
	const int tracks[2] = { 0, 0 };
	EXPECT_TRUE(oakengine_node_keyframes_set_type_many(opacity, "opacity_in", -1,
												  times, tracks, 2, 2) == 2);
	int type = -1;
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 0, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 2);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 2);
	// One undo restores each key's own previous easing (hold at 0, bezier
	// at 15 -- left over from the earlier tests).
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 0, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 2);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(type == 1);

	// A bad address fails without side effects; zero count is a no-op.
	const int64_t bad[1] = { 99 };
	EXPECT_TRUE(oakengine_node_keyframes_set_type_many(opacity, "opacity_in", -1,
												  bad, tracks, 1,
												  1) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_keyframes_set_type_many(opacity, "opacity_in", -1,
												  times, tracks, 0, 1) == 0);
	EXPECT_TRUE(oakengine_node_keyframes_set_type_many(NULL, "opacity_in", -1,
												  times, tracks, 1,
												  1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
}

static void test_keyframe_properties(OakEngineProject *project,
									 OakEngineNode *opacity)
{
	// `opacity` has three keys at 0/15/30 (values 0.0/0.5/1.0) with
	// easings hold/bezier/linear from the earlier tests.
	oak_node_value out;
	int64_t ts = -1;
	const int tr1[1] = { 0 };

	// set_time_many: move 15 -> 20 and undo.
	const int64_t olds[1] = { 15 };
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(opacity, "opacity_in", -1,
												  olds, tr1, 1, 20) == 1);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 20);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(ts == 15);

	// Moving onto an occupied time is a conflict; onto the key's own time
	// is allowed; a missing key is NOT_FOUND. Failures push nothing.
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(opacity, "opacity_in", -1,
												  olds, tr1, 1,
												  30) == OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(opacity, "opacity_in", -1,
												  olds, tr1, 1, 15) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	const int64_t miss[1] = { 99 };
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(opacity, "opacity_in", -1,
												  miss, tr1, 1,
												  40) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(NULL, "opacity_in", -1,
												  olds, tr1, 1,
												  40) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_keyframes_set_time_many(opacity, "opacity_in", -1,
												  olds, tr1, 0, 40) == 0);
	EXPECT_TRUE(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);

	// set_value_many: captured old values are restored by undo.
	const int64_t times2[2] = { 0, 15 };
	const int tr2[2] = { 0, 0 };
	oak_node_value nv[2];
	nv[0] = float_value(0.7);
	nv[1] = float_value(0.6);
	EXPECT_TRUE(oakengine_node_keyframes_set_value_many(opacity, "opacity_in", -1,
												   times2, tr2, 2, nv,
												   NULL) == 2);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.7) < 1e-9);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.6) < 1e-9);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.0) < 1e-9);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 0.5) < 1e-9);

	// Explicit old values (the live-set drag pattern): undo restores the
	// recorded old, redo the new.
	oak_node_value ov[2];
	ov[0] = float_value(9.9);
	ov[1] = float_value(9.8);
	EXPECT_TRUE(oakengine_node_keyframes_set_value_many(opacity, "opacity_in", -1,
												   times2, tr2, 2, nv,
												   ov) == 2);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_at(opacity, "opacity_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out.f[0] - 9.9) < 1e-9);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	oak_node_value orig[2];
	orig[0] = float_value(0.0);
	orig[1] = float_value(0.5);
	EXPECT_TRUE(oakengine_node_keyframes_set_value_many(opacity, "opacity_in", -1,
												   times2, tr2, 2, orig,
												   NULL) == 2);

	// bezier_many: both control points on both keys, undo restores each
	// key's own previous points (key 15 had 0.1/0.2/0.3/0.4).
	EXPECT_TRUE(oakengine_node_keyframes_set_bezier_many(
			   opacity, "opacity_in", -1, times2, tr2, 2, 0.11f, 0.22f, 0.33f,
			   0.44f) == 2);
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	int type = -1;
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(fabsf(x1 - 0.11f) < 1e-6f && fabsf(y1 - 0.22f) < 1e-6f &&
		   fabsf(x2 - 0.33f) < 1e-6f && fabsf(y2 - 0.44f) < 1e-6f);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(fabsf(x1 - 0.1f) < 1e-6f && fabsf(y1 - 0.2f) < 1e-6f);
	EXPECT_TRUE(oakengine_node_keyframes_set_bezier_many(
			   opacity, "opacity_in", -1, miss, tr1, 1, 0.f, 0.f, 0.f,
			   0.f) == OAKENGINE_E_NOT_FOUND);

	// set_bezier_point: NaN old captures the current point.
	EXPECT_TRUE(oakengine_node_keyframe_set_bezier_point(
			   opacity, "opacity_in", -1, 15, 0, 0, 0.5f, 0.6f, NAN,
			   NAN) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(fabsf(x1 - 0.5f) < 1e-6f && fabsf(y1 - 0.6f) < 1e-6f);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(fabsf(x1 - 0.1f) < 1e-6f && fabsf(y1 - 0.2f) < 1e-6f);

	// Explicit old restores the recorded point on undo.
	EXPECT_TRUE(oakengine_node_keyframe_set_bezier_point(
			   opacity, "opacity_in", -1, 15, 0, 0, 0.7f, 0.8f, 9.0f,
			   9.0f) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	EXPECT_TRUE(fabsf(x1 - 9.0f) < 1e-6f);
	// Put the point back so later state matches the earlier tests.
	EXPECT_TRUE(oakengine_node_keyframe_set_bezier_point(
			   opacity, "opacity_in", -1, 15, 0, 0, 0.1f, 0.2f, NAN,
			   NAN) == OAKENGINE_OK);

	// Errors: bad point index, missing key, NULL node.
	EXPECT_TRUE(oakengine_node_keyframe_set_bezier_point(
			   opacity, "opacity_in", -1, 15, 0, 2, 0.f, 0.f, 0.f,
			   0.f) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_keyframe_set_bezier_point(
			   opacity, "opacity_in", -1, 99, 0, 0, 0.f, 0.f, 0.f,
			   0.f) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_keyframe_set_bezier_point(
			   NULL, "opacity_in", -1, 15, 0, 0, 0.f, 0.f, 0.f,
			   0.f) == OAKENGINE_E_INVALID);
}

// Handle-based keyframe family (B8a): enumeration, navigation, handle
// accessors, live mutation, undoable batch operations, detached
// create/paste/dispose and the input dragger.
static void test_handle_family(OakEngineProject *project,
							   OakEngineNode *opacity)
{
	char buf[256];
	oak_node_value v;

	// Start from a clean, keyframing-enabled, empty input.
	EXPECT_TRUE(oakengine_node_keyframes_clear(opacity, "opacity_in") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 0);

	// Toggle ON at 1s: one keyframe with the current value and best type.
	EXPECT_TRUE(oakengine_node_keyframes_toggle_at_time(
			   opacity, "opacity_in", -1, 1, 1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_track_count(opacity, "opacity_in", -1) ==
		   1);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);
	EXPECT_TRUE(oakengine_node_has_keyframe_at_time(opacity, "opacity_in", -1, 1,
											   1) == 1);
	// Toggling on again at the same time is a no-op.
	EXPECT_TRUE(oakengine_node_keyframes_toggle_at_time(
			   opacity, "opacity_in", -1, 1, 1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);

	// More keys via toggles for navigation tests.
	EXPECT_TRUE(oakengine_node_keyframes_toggle_at_time(
			   opacity, "opacity_in", -1, 0, 1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframes_toggle_at_time(
			   opacity, "opacity_in", -1, 3, 1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 3);

	// Navigation.
	int64_t num = -1, den = -1;
	EXPECT_TRUE(oakengine_node_keyframe_earliest_time(opacity, "opacity_in", -1,
												 &num, &den) == 1);
	EXPECT_TRUE(num == 0 && den == 1);
	EXPECT_TRUE(oakengine_node_keyframe_latest_time(opacity, "opacity_in", -1,
											   &num, &den) == 1);
	EXPECT_TRUE(num == 3 && den == 1);
	EXPECT_TRUE(oakengine_node_keyframe_closest_time_before(
			   opacity, "opacity_in", -1, 2, 1, &num, &den) == 1);
	EXPECT_TRUE(num == 1 && den == 1);
	EXPECT_TRUE(oakengine_node_keyframe_closest_time_after(
			   opacity, "opacity_in", -1, 2, 1, &num, &den) == 1);
	EXPECT_TRUE(num == 3 && den == 1);
	EXPECT_TRUE(oakengine_node_keyframe_closest_time_before(
			   opacity, "opacity_in", -1, 0, 1, &num, &den) == 0);
	EXPECT_TRUE(oakengine_node_keyframe_closest_time_after(
			   opacity, "opacity_in", -1, 3, 1, &num, &den) == 0);

	// Handle lookup: on-track enumeration, at-time lookup, and the batch
	// at-time query all agree.
	OakEngineKeyframe *k0 =
		oakengine_node_keyframe_handle_on_track(opacity, "opacity_in", -1, 0, 0);
	OakEngineKeyframe *k1 =
		oakengine_node_keyframe_handle_on_track(opacity, "opacity_in", -1, 0, 1);
	EXPECT_TRUE(k0 != NULL && k1 != NULL && k0 != k1);
	EXPECT_TRUE(oakengine_node_keyframe_handle_on_track(opacity, "opacity_in", -1,
												   0, 3) == NULL);
	EXPECT_TRUE(oakengine_node_keyframe_handle_on_track(opacity, "opacity_in", -1,
												   1, 0) == NULL);
	EXPECT_TRUE(oakengine_node_keyframe_handle_at_time(opacity, "opacity_in", -1,
												  0, 0, 1) == k0);
	EXPECT_TRUE(oakengine_node_keyframe_handle_at_time(opacity, "opacity_in", -1,
												  0, 1, 1) == k1);
	EXPECT_TRUE(oakengine_node_keyframe_handle_at_time(opacity, "opacity_in", -1,
												  0, 2, 1) == NULL);
	OakEngineKeyframe *at[4] = { NULL, NULL, NULL, NULL };
	EXPECT_TRUE(oakengine_node_keyframes_at_time(opacity, "opacity_in", -1, 1, 1,
											at, 4) == 1);
	EXPECT_TRUE(at[0] == k1);

	// Handle accessors.
	EXPECT_TRUE(oakengine_keyframe_get_time(k1, &num, &den) == OAKENGINE_OK);
	EXPECT_TRUE(num == 1 && den == 1);
	EXPECT_TRUE(oakengine_keyframe_get_input_id(k1, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_keyframe_get_track(k1) == 0);
	EXPECT_TRUE(oakengine_keyframe_get_element(k1) == -1);
	EXPECT_TRUE(oakengine_keyframe_get_node(k1) == opacity);
	EXPECT_TRUE(oakengine_keyframe_get_type(k1) >= 0);
	EXPECT_TRUE(oakengine_keyframe_default_type() >= 0);
	EXPECT_TRUE(oakengine_keyframe_get_value(k1, &v) == OAKENGINE_OK);
	EXPECT_TRUE(v.type == OAK_NODE_VALUE_FLOAT);
	// Sibling check: a key at 0s sees the key at 1s and vice versa.
	EXPECT_TRUE(oakengine_keyframe_has_sibling_at_time(k0, 1, 1) == 1);
	EXPECT_TRUE(oakengine_keyframe_has_sibling_at_time(k0, 0, 1) == 0);
	// NULL safety.
	EXPECT_TRUE(oakengine_keyframe_get_time(NULL, &num, &den) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_keyframe_get_type(NULL) == -1);
	EXPECT_TRUE(oakengine_keyframe_get_node(NULL) == NULL);
	EXPECT_TRUE(oakengine_keyframe_get_track(NULL) == -1);
	EXPECT_TRUE(oakengine_keyframe_has_sibling_at_time(NULL, 1, 1) == 0);

	// Bezier points: set easing through the existing API, then live-move a
	// handle (no undo entry) and read it back raw and valid.
	EXPECT_TRUE(oakengine_node_keyframe_add(opacity, "opacity_in", 45, &v, 1,
									   0.1f, 0.2f, 0.3f,
									   0.4f) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_keyframe_set_bezier_point_live(k1, 0, 0.11, 0.22) ==
		   OAKENGINE_OK);
	double x = 0, y = 0;
	EXPECT_TRUE(oakengine_keyframe_get_bezier_point(k1, 0, &x, &y) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(x - 0.11) < 1e-9 && fabs(y - 0.22) < 1e-9);
	EXPECT_TRUE(oakengine_keyframe_get_valid_bezier_point(k1, 0, &x, &y) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_keyframe_get_bezier_point(k1, 2, &x, &y) ==
		   OAKENGINE_E_INVALID);
	// The live move pushed no undo entry of its own: undoing pops the add.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 3);

	// Live value/time mutation.
	EXPECT_TRUE(oakengine_keyframe_set_value_live(k1, &v) == OAKENGINE_OK);
	oak_node_value readback;
	EXPECT_TRUE(oakengine_keyframe_get_value(k1, &readback) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(readback.f[0] - v.f[0]) < 1e-9);
	EXPECT_TRUE(oakengine_keyframe_set_time_live(k1, 2, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_has_keyframe_at_time(opacity, "opacity_in", -1, 2,
											   1) == 1);
	EXPECT_TRUE(oakengine_keyframe_set_time_live(k1, 1, 1) == OAKENGINE_OK);

	// remove_many: delete the keys at 0s and 3s as ONE undoable command.
	OakEngineKeyframe *victims[2] = { k0, oakengine_node_keyframe_handle_at_time(
											opacity, "opacity_in", -1, 0, 3,
											1) };
	EXPECT_TRUE(victims[1] != NULL);
	EXPECT_TRUE(oakengine_keyframes_remove_many(victims, 2, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 3);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);
	// NULL entries are refused and nothing is pushed.
	OakEngineKeyframe *with_null[2] = { k1, NULL };
	EXPECT_TRUE(oakengine_keyframes_remove_many(with_null, 2, NULL) ==
		   OAKENGINE_E_INVALID);

	// Detached create + paste as ONE undoable command, then dispose.
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_FLOAT;
	v.f[0] = 0.33;
	OakEngineKeyframe *detached1 = oakengine_keyframe_create(
		opacity, "opacity_in", -1, 0, 5, 1, &v, 0);
	OakEngineKeyframe *detached2 = oakengine_keyframe_create(
		opacity, "opacity_in", -1, 0, 6, 1, &v, 0);
	OakEngineKeyframe *detached3 = oakengine_keyframe_create(
		opacity, "opacity_in", -1, 0, 7, 1, &v, 0);
	EXPECT_TRUE(detached1 != NULL && detached2 != NULL && detached3 != NULL);
	EXPECT_TRUE(oakengine_keyframe_create(opacity, "no_such", -1, 0, 5, 1, &v,
									 0) == NULL);
	v.f[0] = 1.5;
	OakEngineKeyframe *both[2] = { detached1, detached2 };
	EXPECT_TRUE(oakengine_node_keyframes_paste(opacity, both, 2, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 3);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 3);
	oakengine_keyframe_dispose(detached3);
	oakengine_keyframe_dispose(NULL); // no-op

	// Toggle OFF the key at 1s: removed, single-track standard value fix-up.
	EXPECT_TRUE(oakengine_node_keyframes_toggle_at_time(
			   opacity, "opacity_in", -1, 1, 1, 0, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_has_keyframe_at_time(opacity, "opacity_in", -1, 1,
											   1) == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_has_keyframe_at_time(opacity, "opacity_in", -1, 1,
											   1) == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);

	// Disable keyframing entirely: all keys gone, keyframing flag off.
	EXPECT_TRUE(oakengine_node_set_input_keyframing(opacity, "opacity_in", -1, 0,
											   1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 0);
	// Re-enable through the facade: one default-type key per track.
	EXPECT_TRUE(oakengine_node_set_input_keyframing(opacity, "opacity_in", -1, 1,
											   1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_keyframed_ex(opacity, "opacity_in", -1) ==
		   1);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);
	OakEngineKeyframe *sole = oakengine_node_keyframe_handle_on_track(
		opacity, "opacity_in", -1, 0, 0);
	EXPECT_TRUE(oakengine_keyframe_get_type(sole) ==
		   oakengine_keyframe_default_type());
	// Redundant enable is a no-op success.
	EXPECT_TRUE(oakengine_node_set_input_keyframing(opacity, "opacity_in", -1, 1,
											   1, 1, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == 1);
	// Undo both steps back to keyframing disabled, then redo to enabled.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 1);

	// Input dragger: start creates a key at the drag time, drag live-sets,
	// end pushes ONE undoable command.
	OakEngineNodeDragger *dragger =
		oakengine_dragger_create(opacity, "opacity_in", -1, 0);
	EXPECT_TRUE(dragger != NULL);
	EXPECT_TRUE(oakengine_dragger_create(opacity, "no_such", -1, 0) == NULL);
	EXPECT_TRUE(oakengine_dragger_is_started(dragger) == 0);
	EXPECT_TRUE(oakengine_dragger_end(dragger, NULL) == OAKENGINE_E_STATE);
	const int keys_before =
		oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1, 0);
	EXPECT_TRUE(oakengine_dragger_start(dragger, 4, 1, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_dragger_is_started(dragger) == 1);
	EXPECT_TRUE(oakengine_dragger_start(dragger, 4, 1, 1) == OAKENGINE_E_STATE);
	oak_node_value drag_value;
	memset(&drag_value, 0, sizeof(drag_value));
	drag_value.type = OAK_NODE_VALUE_FLOAT;
	drag_value.f[0] = 0.9;
	EXPECT_TRUE(oakengine_dragger_drag(dragger, &drag_value) == OAKENGINE_OK);
	oak_node_value at_time;
	EXPECT_TRUE(oakengine_node_get_input_at_time(opacity, "opacity_in", -1, 0, 4,
											1, &at_time) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(at_time.f[0] - 0.9) < 1e-9);
	EXPECT_TRUE(oakengine_dragger_end(dragger, "Drag Opacity") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_dragger_is_started(dragger) == 0);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) ==
		   keys_before + 1);
	// The whole drag (created key + value) unwinds with one undo.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_keyframe_count_on_track(opacity, "opacity_in", -1,
												  0) == keys_before);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	oakengine_dragger_free(dragger);
	oakengine_dragger_free(NULL);

	// Clean up for later tests.
	EXPECT_TRUE(oakengine_node_keyframes_clear(opacity, "opacity_in") ==
		   OAKENGINE_OK);
}

TEST(OakEngineKeyframe, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("OAK_CONFIG_DIR", g_tmpdir, 1) == 0);
#endif

	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	// A sequence provides the frame-rate timebase for keyframe timestamps.
	OakEngineSequence *seq = oakengine_sequence_new(project, "Seq");
	EXPECT_TRUE(seq != NULL);

	OakEngineNode *opacity = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.opacity");
	EXPECT_TRUE(opacity != NULL);
	OakEngineNode *timeremap = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.timeremap");
	EXPECT_TRUE(timeremap != NULL);
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);

	test_float_lifecycle(project, opacity);
	test_easing_and_remove(project, opacity);
	test_rational_and_color(project, timeremap, solid);
	test_panel_paths(project, opacity, solid);
	test_keyframe_properties(project, opacity);
	test_handle_family(project, opacity);

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
