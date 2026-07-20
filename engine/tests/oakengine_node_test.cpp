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

// Pure C ABI test for the liboakengine node-graph facade: enumeration,
// metadata, input introspection, parameter get/set (POD + string), edge
// connect/disconnect and node add/remove -- all through their undo/redo
// behavior too. Uses the Solid generator and the OCIO LUT nodes as
// fixtures. No GL required.

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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_node_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_node_test_XXXXXX");
	assert(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void test_enumeration(OakEngineProject *project)
{
	// A fresh project holds at least its root folder node.
	const int count = oakengine_project_node_count(project);
	assert(count >= 1);
	assert(oakengine_project_node_at(project, 0) != NULL);
	assert(oakengine_project_node_at(project, count - 1) != NULL);
	assert(oakengine_project_node_at(project, count) == NULL);
	assert(oakengine_project_node_at(project, -1) == NULL);

	// NULL safety.
	assert(oakengine_project_node_count(NULL) == 0);
	assert(oakengine_project_node_at(NULL, 0) == NULL);
	assert(oakengine_node_get_type_id(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_get_name(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_get_label(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_set_label(NULL, "x") == OAKENGINE_E_INVALID);
	assert(oakengine_node_input_count(NULL) == 0);
	assert(oakengine_node_input_id(NULL, 0, NULL, 0) == OAKENGINE_E_INVALID);
	assert(oakengine_node_input_get_type(NULL, "x") == OAK_NODE_VALUE_NONE);
	assert(oakengine_node_input_is_connected(NULL, "x") == 0);
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(NULL, "x", &v) == OAKENGINE_E_INVALID);
	assert(oakengine_node_set_input(NULL, "x", &v) == OAKENGINE_E_INVALID);
	assert(oakengine_node_get_input_string(NULL, "x", NULL, 0) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_node_set_input_string(NULL, "x", "y") ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_project_add_node(NULL, "x") == NULL);
	assert(oakengine_project_remove_node(NULL, NULL) == OAKENGINE_E_INVALID);
	assert(oakengine_node_connect(NULL, NULL, "x") == OAKENGINE_E_INVALID);
	assert(oakengine_node_disconnect(NULL, "x") == OAKENGINE_E_INVALID);
}

static void test_add_and_metadata(OakEngineProject *project,
								  OakEngineNode **solid_out,
								  OakEngineNode **lut_out)
{
	const int before = oakengine_project_node_count(project);
	char buf[256];

	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	assert(solid != NULL);
	assert(oakengine_project_node_count(project) == before + 1);

	assert(oakengine_node_get_type_id(solid, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "org.olivevideoeditor.Olive.solidgenerator") == 0);
	assert(oakengine_node_get_name(solid, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "Solid") == 0);

	// Unknown type id fails with a reason.
	assert(oakengine_project_add_node(project, "org.example.nonexistent") ==
		   NULL);
	char err[256];
	assert(oakengine_node_last_error(err, sizeof(err)) > 0);

	// Label round-trip with undo/redo.
	assert(oakengine_node_get_label(solid, buf, sizeof(buf)) >= 0);
	assert(oakengine_node_set_label(solid, "MySolid") == OAKENGINE_OK);
	assert(oakengine_node_get_label(solid, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "MySolid") == 0);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_label(solid, buf, sizeof(buf)) >= 0);
	assert(strcmp(buf, "MySolid") != 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_label(solid, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "MySolid") == 0);

	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	assert(lut != NULL);

	*solid_out = solid;
	*lut_out = lut;
}

static void test_inputs_and_params(OakEngineProject *project,
								   OakEngineNode *solid, OakEngineNode *lut)
{
	char buf[256];

	// Introspection on the Solid generator: the Node base class provides
	// "enabled_in" (BOOL), the generator itself adds "color_in" (COLOR).
	const int inputs = oakengine_node_input_count(solid);
	assert(inputs >= 2);
	assert(oakengine_node_input_id(solid, 0, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "enabled_in") == 0);
	assert(oakengine_node_input_id(solid, 1, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "color_in") == 0);
	assert(oakengine_node_input_id(solid, inputs, buf, sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_input_get_type(solid, "enabled_in") ==
		   OAK_NODE_VALUE_BOOL);
	assert(oakengine_node_input_get_type(solid, "color_in") ==
		   OAK_NODE_VALUE_COLOR);
	assert(oakengine_node_input_get_type(solid, "no_such_input") ==
		   OAK_NODE_VALUE_NONE);
	assert(oakengine_node_input_is_connected(solid, "color_in") == 0);

	// BOOL get/set on the base-class input (defaults to true).
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	assert(v.type == OAK_NODE_VALUE_BOOL && v.num == 1);
	v.num = 0;
	assert(oakengine_node_set_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	assert(v.num == 0);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	assert(v.num == 1);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);

	// COLOR get/set round-trip (Solid defaults to opaque red).
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	assert(v.type == OAK_NODE_VALUE_COLOR);
	assert(fabs(v.f[0] - 1.0) < 1e-6 && fabs(v.f[3] - 1.0) < 1e-6);

	v.f[0] = 0.2;
	v.f[1] = 0.4;
	v.f[2] = 0.6;
	v.f[3] = 1.0;
	assert(oakengine_node_set_input(solid, "color_in", &v) == OAKENGINE_OK);
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	assert(fabs(v.f[0] - 0.2) < 1e-6 && fabs(v.f[1] - 0.4) < 1e-6 &&
		   fabs(v.f[2] - 0.6) < 1e-6);

	// Undo restores the default, redo applies it again.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	assert(fabs(v.f[0] - 1.0) < 1e-6);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	assert(fabs(v.f[0] - 0.2) < 1e-6);

	// Type errors: INT into a COLOR input, POD into a string input, POD and
	// string APIs on an unknown id.
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_INT;
	v.num = 3;
	assert(oakengine_node_set_input(solid, "color_in", &v) ==
		   OAKENGINE_E_INVALID);
	v.type = OAK_NODE_VALUE_COLOR;
	assert(oakengine_node_set_input(solid, "no_such_input", &v) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_get_input(solid, "no_such_input", &v) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_set_input_string(solid, "color_in", "x") ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_node_get_input_string(solid, "color_in", buf,
										   sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// STRING (k_file) on the LUT node: empty by default, round-trip + undo.
	assert(oakengine_node_input_get_type(lut, "lut_file_in") ==
		   OAK_NODE_VALUE_STRING);
	assert(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) == 0);
	assert(oakengine_node_set_input_string(lut, "lut_file_in",
										   "/tmp/x.cube") == OAKENGINE_OK);
	assert(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) > 0);
	assert(strcmp(buf, "/tmp/x.cube") == 0);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) > 0);
	assert(strcmp(buf, "/tmp/x.cube") == 0);

	// String typed inputs are rejected by the POD pair.
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(lut, "lut_file_in", &v) ==
		   OAKENGINE_E_INVALID);
	v.type = OAK_NODE_VALUE_STRING;
	assert(oakengine_node_set_input(lut, "lut_file_in", &v) ==
		   OAKENGINE_E_INVALID);

	// COMBO on the LUT direction input: 0 by default, set 1, undo.
	assert(oakengine_node_input_get_type(lut, "lut_dir_in") ==
		   OAK_NODE_VALUE_COMBO);
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	assert(v.type == OAK_NODE_VALUE_COMBO && v.num == 0);
	v.num = 1;
	assert(oakengine_node_set_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	memset(&v, 0, sizeof(v));
	assert(oakengine_node_get_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	assert(v.num == 1);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	assert(v.num == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
}

static void test_edges(OakEngineProject *project, OakEngineNode *solid,
					   OakEngineNode *lut)
{
	char err[256];

	// Solid's texture output -> LUT's "tex_in".
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	assert(oakengine_node_connect(solid, lut, "tex_in") == OAKENGINE_OK);
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 1);

	// Already-connected input is refused; unknown ids and unconnectable
	// inputs fail.
	assert(oakengine_node_connect(solid, lut, "tex_in") ==
		   OAKENGINE_E_STATE);
	assert(oakengine_node_connect(solid, lut, "no_such_input") ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_node_connect(solid, lut, "lut_file_in") ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_node_last_error(err, sizeof(err)) > 0);

	// Disconnect restores the unconnected state; a second disconnect fails.
	assert(oakengine_node_disconnect(lut, "tex_in") == OAKENGINE_OK);
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	assert(oakengine_node_disconnect(lut, "tex_in") ==
		   OAKENGINE_E_NOT_FOUND);

	// Undo/redo the disconnect and the connect: undo brings the connection
	// back, undo again removes it; redoing both replays connect then
	// disconnect, so the end state is disconnected.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 1);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 1);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_node_input_is_connected(lut, "tex_in") == 0);
}

static void test_remove(OakEngineProject *project, OakEngineNode *solid,
						OakEngineNode *lut)
{
	const int before = oakengine_project_node_count(project);

	// A node from another project is refused.
	OakEngineProject *other = oakengine_project_create();
	assert(other != NULL);
	assert(oakengine_project_new(other) == OAKENGINE_OK);
	assert(oakengine_project_remove_node(other, solid) ==
		   OAKENGINE_E_INVALID);
	oakengine_project_free(other);

	assert(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
	assert(oakengine_project_node_count(project) == before - 1);

	// Undo brings the node back, redo removes it again.
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_project_node_count(project) == before);
	assert(oakengine_project_redo(project) == OAKENGINE_OK);
	assert(oakengine_project_node_count(project) == before - 1);

	(void)solid;
}

static void test_label_and_color_many(OakEngineProject *project)
{
	OakEngineNode *a = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *b = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.text3");
	assert(a != NULL && b != NULL);
	OakEngineNode *two[2] = { a, b };
	char buf[128];

	// One command renames both; undo restores each node's own label.
	assert(oakengine_node_set_label_many(two, 2, "Shared") == OAKENGINE_OK);
	assert(oakengine_node_get_label(a, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "Shared") == 0);
	assert(oakengine_node_get_label(b, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "Shared") == 0);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	// The original labels were empty.
	assert(oakengine_node_get_label(a, buf, sizeof(buf)) == 0);
	assert(oakengine_node_get_label(b, buf, sizeof(buf)) == 0);
	assert(oakengine_node_set_label_many(NULL, 1, "x") ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_node_set_label_many(two, 0, "x") == OAKENGINE_OK);

	// Color labels batch in one command too.
	assert(oakengine_node_get_color_label(a) == -1);
	assert(oakengine_node_set_color_label(two, 2, 5) == OAKENGINE_OK);
	assert(oakengine_node_get_color_label(a) == 5);
	assert(oakengine_node_get_color_label(b) == 5);
	assert(oakengine_project_undo(project) == OAKENGINE_OK);
	assert(oakengine_node_get_color_label(a) == -1);
	assert(oakengine_node_get_color_label(b) == -1);
	assert(oakengine_node_get_color_label(NULL) == -1);
	assert(oakengine_node_set_color_label(NULL, 1, 1) ==
		   OAKENGINE_E_INVALID);
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

	test_enumeration(project);

	OakEngineNode *solid = NULL, *lut = NULL;
	test_add_and_metadata(project, &solid, &lut);
	test_inputs_and_params(project, solid, lut);
	test_edges(project, solid, lut);
	test_remove(project, solid, lut);
	test_label_and_color_many(project);

	// Graph nodes are not timeline clips: a sequence's track list stays
	// empty no matter what the project graph holds.
	OakEngineSequence *seq = oakengine_sequence_new(project, "Seq");
	assert(seq != NULL);
	int video = -1, audio = -1, subtitle = -1;
	assert(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	assert(video == 0 && audio == 0 && subtitle == 0);

	oakengine_project_free(project);
	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_node_test: all assertions passed\n");
	return 0;
}
