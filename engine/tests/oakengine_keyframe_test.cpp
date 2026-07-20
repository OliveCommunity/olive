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
	assert(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_keyframe_test_%lu",
			 base, (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_keyframe_test_XXXXXX");
	assert(mkdtemp(g_tmpdir) != NULL);
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

	assert(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 0);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 0);

	// Missing input ids and missing keyframes report errors.
	oak_node_value v = float_value(0.5);
	assert(oakengine_node_keyframe_add(opacity, "no_such_input", 0, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_last_error(err, sizeof(err)) > 0);
	assert(oakengine_node_keyframe_add(NULL, "opacity_in", 0, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_keyframe_add(opacity, "opacity_in", 0, NULL, 0, 0,
									   0, 0, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_keyframe_add(opacity, "opacity_in", 0, &v, 9, 0, 0,
									   0, 0) == OAKENGINE_E_INVALID);

	// Three linear keys at 0, 15, 30.
	v = float_value(0.0);
	assert(oakengine_node_keyframe_add(opacity, "opacity_in", 0, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_OK);
	assert(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 1);
	v = float_value(0.5);
	assert(oakengine_node_keyframe_add(opacity, "opacity_in", 15, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_OK);
	v = float_value(1.0);
	assert(oakengine_node_keyframe_add(opacity, "opacity_in", 30, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);

	// A key at an occupied time is refused.
	v = float_value(0.25);
	assert(oakengine_node_keyframe_add(opacity, "opacity_in", 15, &v, 0, 0,
									   0, 0, 0) == OAKENGINE_E_STATE);

	// Read them back: ordered, correct times and values, linear easing.
	int64_t ts = -1;
	oak_node_value out;
	for (int i = 0; i < 3; i++) {
		assert(oakengine_node_keyframe_at(opacity, "opacity_in", i, &ts,
										  &out) == OAKENGINE_OK);
		assert(ts == i * 15);
		assert(out.type == OAK_NODE_VALUE_FLOAT);
		assert(fabs(out.f[0] - i * 0.5) < 1e-9);
		int type = -1;
		float x1 = -1, y1 = -1, x2 = -1, y2 = -1;
		assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", i,
												  &x1, &y1, &x2, &y2,
												  &type) == OAKENGINE_OK);
		assert(type == 0); // linear
		assert(x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0);
	}
	assert(oakengine_node_keyframe_at(opacity, "opacity_in", 3, &ts, &out) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 99,
											  NULL, NULL, NULL, NULL,
											  NULL) == OAKENGINE_E_NOT_FOUND);

	// Undo the adds one by one, then redo them all.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 2);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 1);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 0);
	assert(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
	assert(oakengine_node_input_is_keyframed(opacity, "opacity_in") == 1);
}

static void test_easing_and_remove(OakEngineProject *project,
								   OakEngineNode *opacity)
{
	// Bezier easing on the middle key, control points round-trip.
	assert(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 15, 1,
											  0.1f, 0.2f, 0.3f,
											  0.4f) == OAKENGINE_OK);
	int type = -1;
	float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	assert(type == 1); // bezier
	assert(fabsf(x1 - 0.1f) < 1e-6f && fabsf(y1 - 0.2f) < 1e-6f);
	assert(fabsf(x2 - 0.3f) < 1e-6f && fabsf(y2 - 0.4f) < 1e-6f);

	// Undo restores linear, redo restores bezier.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	assert(type == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, &x1,
											  &y1, &x2, &y2,
											  &type) == OAKENGINE_OK);
	assert(type == 1);

	// Hold easing on the first key.
	assert(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 0, 2, 0,
											  0, 0, 0) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 0, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	assert(type == 2); // hold
	assert(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 15, 9, 0,
											  0, 0, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_keyframe_set_easing(opacity, "opacity_in", 77, 1, 0,
											  0, 0,
											  0) == OAKENGINE_E_NOT_FOUND);

	// Remove the middle key; order compacts.
	assert(oakengine_node_keyframe_remove(opacity, "opacity_in", 15) ==
		   OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 2);
	int64_t ts = -1;
	oak_node_value out;
	assert(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, &out) ==
		   OAKENGINE_OK);
	assert(ts == 30 && fabs(out.f[0] - 1.0) < 1e-9);
	assert(oakengine_node_keyframe_remove(opacity, "opacity_in", 15) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
	assert(oakengine_node_keyframe_at(opacity, "opacity_in", 1, &ts, &out) ==
		   OAKENGINE_OK);
	assert(ts == 15);

	// Clear all, then undo.
	assert(oakengine_node_keyframes_clear(opacity, "opacity_in") ==
		   OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 0);
	assert(oakengine_node_keyframes_clear(opacity, "opacity_in") ==
		   OAKENGINE_OK); // no-op on empty
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
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
	assert(oakengine_node_keyframe_add(timeremap, "time_in", 0, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(timeremap, "time_in") == 1);
	int64_t ts = -1;
	oak_node_value out;
	assert(oakengine_node_keyframe_at(timeremap, "time_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	assert(ts == 0);
	assert(out.type == OAK_NODE_VALUE_RATIONAL);
	assert(out.num == 30 && out.den == 1);

	// A type mismatch is rejected.
	v.type = OAK_NODE_VALUE_FLOAT;
	v.f[0] = 1.5;
	assert(oakengine_node_keyframe_add(timeremap, "time_in", 5, &v, 0, 0, 0,
									   0, 0) == OAKENGINE_E_INVALID);

	// COLOR on the Solid generator: track 0 is the red component.
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_COLOR;
	v.f[0] = 0.25;
	v.f[1] = 0.5;
	v.f[2] = 0.75;
	v.f[3] = 1.0;
	assert(oakengine_node_keyframe_add(solid, "color_in", 10, &v, 0, 0, 0, 0,
									   0) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_at(solid, "color_in", 0, &ts, &out) ==
		   OAKENGINE_OK);
	assert(ts == 10);
	assert(out.type == OAK_NODE_VALUE_COLOR);
	assert(fabs(out.f[0] - 0.25) < 1e-6); // first component only

	assert(oakengine_node_keyframes_clear(timeremap, "time_in") ==
		   OAKENGINE_OK);
	assert(oakengine_node_keyframes_clear(solid, "color_in") == OAKENGINE_OK);
}

static void test_panel_paths(OakEngineProject *project,
							 OakEngineNode *opacity, OakEngineNode *solid)
{
	oak_node_value out;

	// Frame time base (1001/30000 with the default sequence).
	int tbn = 0, tbd = 0;
	assert(oakengine_node_frame_time_base(opacity, &tbn, &tbd) ==
		   OAKENGINE_OK);
	assert(tbn > 0 && tbd > 0);
	assert(oakengine_node_frame_time_base(NULL, &tbn, &tbd) ==
		   OAKENGINE_E_INVALID);

	// set_input_at_time on a NON-keyframed input (fresh node) sets the
	// standard value; undo restores it.
	OakEngineNode *op2 = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.opacity");
	assert(op2 != NULL);
	oak_node_value v = float_value(0.75);
	assert(oakengine_node_set_input_at_time(op2, "opacity_in", -1, 10, 0, &v,
											1) == OAKENGINE_OK);
	assert(oakengine_node_get_input(op2, "opacity_in", &out) == OAKENGINE_OK);
	assert(fabs(out.f[0] - 0.75) < 1e-9);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input(op2, "opacity_in", &out) == OAKENGINE_OK);
	assert(fabs(out.f[0] - 0.75) > 1e-9);

	// Component type must match the declared type; bad track/id rejected.
	oak_node_value wrong;
	memset(&wrong, 0, sizeof(wrong));
	wrong.type = OAK_NODE_VALUE_INT;
	assert(oakengine_node_set_input_at_time(op2, "opacity_in", -1, 0, 0,
											&wrong,
											1) == OAKENGINE_E_INVALID);
	assert(oakengine_node_set_input_at_time(op2, "opacity_in", -1, 0, 1, &v,
											1) == OAKENGINE_E_INVALID);
	assert(oakengine_node_set_input_at_time(op2, "nope", -1, 0, 0, &v,
											1) == OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_set_input_at_time(NULL, "opacity_in", -1, 0, 0, &v,
											1) == OAKENGINE_E_INVALID);

	// `opacity` is keyframed at this point (keys at 0/15/30 left by the
	// earlier tests): the call writes a KEYFRAME at the time
	// (set_value_at_time semantics) -- insert first, then update in place.
	v = float_value(0.9);
	assert(oakengine_node_set_input_at_time(opacity, "opacity_in", -1, 20, 0,
											&v, 1) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 4);
	int64_t ts = -1;
	assert(oakengine_node_keyframe_at(opacity, "opacity_in", 2, &ts, &out) ==
		   OAKENGINE_OK);
	assert(ts == 20 && fabs(out.f[0] - 0.9) < 1e-9);
	v = float_value(0.8);
	assert(oakengine_node_set_input_at_time(opacity, "opacity_in", -1, 20, 0,
											&v, 1) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 4);
	assert(oakengine_node_keyframe_at(opacity, "opacity_in", 2, &ts, &out) ==
		   OAKENGINE_OK);
	assert(fabs(out.f[0] - 0.8) < 1e-9);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);

	// track -1 writes all color components in one command (fresh node, so
	// the standard value itself is written and readable back).
	OakEngineNode *solid2 = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	assert(solid2 != NULL);
	oak_node_value c;
	memset(&c, 0, sizeof(c));
	c.type = OAK_NODE_VALUE_COLOR;
	c.f[0] = 0.1;
	c.f[1] = 0.2;
	c.f[2] = 0.3;
	c.f[3] = 0.4;
	assert(oakengine_node_set_input_at_time(solid2, "color_in", -1, 5, -1, &c,
											0) == OAKENGINE_OK);
	assert(oakengine_node_get_input(solid2, "color_in", &out) ==
		   OAKENGINE_OK);
	assert(fabs(out.f[0] - 0.1) < 1e-6 && fabs(out.f[1] - 0.2) < 1e-6 &&
		   fabs(out.f[2] - 0.3) < 1e-6 && fabs(out.f[3] - 0.4) < 1e-6);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);

	// String-at-time on the text node; the POD path rejects strings and
	// vice versa.
	OakEngineNode *text = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.text3");
	assert(text != NULL);
	assert(oakengine_node_set_input_string_at_time(text, "text_in", -1, 0,
												   "hello") == OAKENGINE_OK);
	assert(oakengine_node_set_input_string_at_time(text, "nope", -1, 0,
												   "x") == OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_set_input_string_at_time(op2, "opacity_in", -1, 0,
												   "x") == OAKENGINE_E_INVALID);
	assert(oakengine_node_set_input_at_time(text, "text_in", -1, 0, 0, &v,
											1) == OAKENGINE_E_INVALID);

	// Array insert/remove on the text node's args_in array.
	assert(oakengine_node_array_insert_at(text, "args_in", 0) ==
		   OAKENGINE_OK);
	assert(oakengine_node_array_insert_at(text, "args_in", 1) ==
		   OAKENGINE_OK);
	assert(oakengine_node_array_remove_at(text, "args_in", 1) ==
		   OAKENGINE_OK);
	assert(oakengine_node_array_remove_at(text, "args_in", 5) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_array_remove_at(text, "args_in", 0) ==
		   OAKENGINE_OK);
	assert(oakengine_node_array_insert_at(text, "args_in", -1) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_node_array_remove_at(NULL, "args_in", 0) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);

	// keyframes_set_type_many: keys at 0 and 15 to hold in one command.
	const int64_t times[2] = { 0, 15 };
	const int tracks[2] = { 0, 0 };
	assert(oakengine_node_keyframes_set_type_many(opacity, "opacity_in", -1,
												  times, tracks, 2, 2) == 2);
	int type = -1;
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 0, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	assert(type == 2);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	assert(type == 2);
	// One undo restores each key's own previous easing (hold at 0, bezier
	// at 15 -- left over from the earlier tests).
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 0, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	assert(type == 2);
	assert(oakengine_node_keyframe_get_easing(opacity, "opacity_in", 1, NULL,
											  NULL, NULL, NULL,
											  &type) == OAKENGINE_OK);
	assert(type == 1);

	// A bad address fails without side effects; zero count is a no-op.
	const int64_t bad[1] = { 99 };
	assert(oakengine_node_keyframes_set_type_many(opacity, "opacity_in", -1,
												  bad, tracks, 1,
												  1) == OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_keyframes_set_type_many(opacity, "opacity_in", -1,
												  times, tracks, 0, 1) == 0);
	assert(oakengine_node_keyframes_set_type_many(NULL, "opacity_in", -1,
												  times, tracks, 1,
												  1) == OAKENGINE_E_INVALID);
	assert(oakengine_node_keyframe_count(opacity, "opacity_in") == 3);
}

int main(void)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	assert(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);
	// A sequence provides the frame-rate timebase for keyframe timestamps.
	OakEngineSequence *seq = oakengine_sequence_new(project, "Seq");
	assert(seq != NULL);

	OakEngineNode *opacity = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.opacity");
	assert(opacity != NULL);
	OakEngineNode *timeremap = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.timeremap");
	assert(timeremap != NULL);
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	assert(solid != NULL);

	test_float_lifecycle(project, opacity);
	test_easing_and_remove(project, opacity);
	test_rational_and_color(project, timeremap, solid);
	test_panel_paths(project, opacity, solid);

	oakengine_project_free(project);
	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_keyframe_test: all assertions passed\n");
	return 0;
}
