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
#include "oakengine/undo.h"

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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_node_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_node_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void test_enumeration(OakEngineProject *project)
{
	// A fresh project holds at least its root folder node.
	const int count = oakengine_project_node_count(project);
	EXPECT_TRUE(count >= 1);
	EXPECT_TRUE(oakengine_project_node_at(project, 0) != NULL);
	EXPECT_TRUE(oakengine_project_node_at(project, count - 1) != NULL);
	EXPECT_TRUE(oakengine_project_node_at(project, count) == NULL);
	EXPECT_TRUE(oakengine_project_node_at(project, -1) == NULL);

	// NULL safety.
	EXPECT_TRUE(oakengine_project_node_count(NULL) == 0);
	EXPECT_TRUE(oakengine_project_node_at(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_node_get_type_id(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_get_name(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_get_label(NULL, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_label(NULL, "x") == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_input_count(NULL) == 0);
	EXPECT_TRUE(oakengine_node_input_id(NULL, 0, NULL, 0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_input_get_type(NULL, "x") == OAK_NODE_VALUE_NONE);
	EXPECT_TRUE(oakengine_node_input_is_connected(NULL, "x") == 0);
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(NULL, "x", &v) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_input(NULL, "x", &v) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_get_input_string(NULL, "x", NULL, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_input_string(NULL, "x", "y") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_project_add_node(NULL, "x") == NULL);
	EXPECT_TRUE(oakengine_project_remove_node(NULL, NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_connect(NULL, NULL, "x") == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_disconnect(NULL, "x") == OAKENGINE_E_INVALID);
}

static void test_add_and_metadata(OakEngineProject *project,
								  OakEngineNode **solid_out,
								  OakEngineNode **lut_out)
{
	const int before = oakengine_project_node_count(project);
	char buf[256];

	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);
	EXPECT_TRUE(oakengine_project_node_count(project) == before + 1);

	EXPECT_TRUE(oakengine_node_get_type_id(solid, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "org.olivevideoeditor.Olive.solidgenerator") == 0);
	EXPECT_TRUE(oakengine_node_get_name(solid, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "Solid") == 0);

	// Unknown type id fails with a reason.
	EXPECT_TRUE(oakengine_project_add_node(project, "org.example.nonexistent") ==
		   NULL);
	char err[256];
	EXPECT_TRUE(oakengine_node_last_error(err, sizeof(err)) > 0);

	// Label round-trip with undo/redo.
	EXPECT_TRUE(oakengine_node_get_label(solid, buf, sizeof(buf)) >= 0);
	EXPECT_TRUE(oakengine_node_set_label(solid, "MySolid") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_label(solid, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "MySolid") == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_label(solid, buf, sizeof(buf)) >= 0);
	EXPECT_TRUE(strcmp(buf, "MySolid") != 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_label(solid, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "MySolid") == 0);

	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	EXPECT_TRUE(lut != NULL);

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
	EXPECT_TRUE(inputs >= 2);
	EXPECT_TRUE(oakengine_node_input_id(solid, 0, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "enabled_in") == 0);
	EXPECT_TRUE(oakengine_node_input_id(solid, 1, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "color_in") == 0);
	EXPECT_TRUE(oakengine_node_input_id(solid, inputs, buf, sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_input_get_type(solid, "enabled_in") ==
		   OAK_NODE_VALUE_BOOL);
	EXPECT_TRUE(oakengine_node_input_get_type(solid, "color_in") ==
		   OAK_NODE_VALUE_COLOR);
	EXPECT_TRUE(oakengine_node_input_get_type(solid, "no_such_input") ==
		   OAK_NODE_VALUE_NONE);
	EXPECT_TRUE(oakengine_node_input_is_connected(solid, "color_in") == 0);

	// BOOL get/set on the base-class input (defaults to true).
	oak_node_value v;
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(v.type == OAK_NODE_VALUE_BOOL && v.num == 1);
	v.num = 0;
	EXPECT_TRUE(oakengine_node_set_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(v.num == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(solid, "enabled_in", &v) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(v.num == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);

	// COLOR get/set round-trip (Solid defaults to opaque red).
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(v.type == OAK_NODE_VALUE_COLOR);
	EXPECT_TRUE(fabs(v.f[0] - 1.0) < 1e-6 && fabs(v.f[3] - 1.0) < 1e-6);

	v.f[0] = 0.2;
	v.f[1] = 0.4;
	v.f[2] = 0.6;
	v.f[3] = 1.0;
	EXPECT_TRUE(oakengine_node_set_input(solid, "color_in", &v) == OAKENGINE_OK);
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(v.f[0] - 0.2) < 1e-6 && fabs(v.f[1] - 0.4) < 1e-6 &&
		   fabs(v.f[2] - 0.6) < 1e-6);

	// Undo restores the default, redo applies it again.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(v.f[0] - 1.0) < 1e-6);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(solid, "color_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(v.f[0] - 0.2) < 1e-6);

	// Type errors: INT into a COLOR input, POD into a string input, POD and
	// string APIs on an unknown id.
	memset(&v, 0, sizeof(v));
	v.type = OAK_NODE_VALUE_INT;
	v.num = 3;
	EXPECT_TRUE(oakengine_node_set_input(solid, "color_in", &v) ==
		   OAKENGINE_E_INVALID);
	v.type = OAK_NODE_VALUE_COLOR;
	EXPECT_TRUE(oakengine_node_set_input(solid, "no_such_input", &v) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_get_input(solid, "no_such_input", &v) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_set_input_string(solid, "color_in", "x") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_get_input_string(solid, "color_in", buf,
										   sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// STRING (k_file) on the LUT node: empty by default, round-trip + undo.
	EXPECT_TRUE(oakengine_node_input_get_type(lut, "lut_file_in") ==
		   OAK_NODE_VALUE_STRING);
	EXPECT_TRUE(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) == 0);
	EXPECT_TRUE(oakengine_node_set_input_string(lut, "lut_file_in",
										   "/tmp/x.cube") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "/tmp/x.cube") == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input_string(lut, "lut_file_in", buf,
										   sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "/tmp/x.cube") == 0);

	// String typed inputs are rejected by the POD pair.
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(lut, "lut_file_in", &v) ==
		   OAKENGINE_E_INVALID);
	v.type = OAK_NODE_VALUE_STRING;
	EXPECT_TRUE(oakengine_node_set_input(lut, "lut_file_in", &v) ==
		   OAKENGINE_E_INVALID);

	// COMBO on the LUT direction input: 0 by default, set 1, undo.
	EXPECT_TRUE(oakengine_node_input_get_type(lut, "lut_dir_in") ==
		   OAK_NODE_VALUE_COMBO);
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(v.type == OAK_NODE_VALUE_COMBO && v.num == 0);
	v.num = 1;
	EXPECT_TRUE(oakengine_node_set_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	memset(&v, 0, sizeof(v));
	EXPECT_TRUE(oakengine_node_get_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(v.num == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(lut, "lut_dir_in", &v) == OAKENGINE_OK);
	EXPECT_TRUE(v.num == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
}

static void test_edges(OakEngineProject *project, OakEngineNode *solid,
					   OakEngineNode *lut)
{
	char err[256];

	// Solid's texture output -> LUT's "tex_in".
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 1);

	// Already-connected input is refused; unknown ids and unconnectable
	// inputs fail.
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "tex_in") ==
		   OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "no_such_input") ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "lut_file_in") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_last_error(err, sizeof(err)) > 0);

	// Disconnect restores the unconnected state; a second disconnect fails.
	EXPECT_TRUE(oakengine_node_disconnect(lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	EXPECT_TRUE(oakengine_node_disconnect(lut, "tex_in") ==
		   OAKENGINE_E_NOT_FOUND);

	// disconnect_ex with element -1 mirrors disconnect(); on an unconnected
	// input it reports E_NOT_FOUND. NULL/unknown-input rejection matches
	// disconnect() too.
	EXPECT_TRUE(oakengine_node_disconnect_ex(NULL, "tex_in", -1) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_disconnect_ex(lut, "no_such_input", -1) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_disconnect_ex(lut, "tex_in", -1) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_disconnect_ex(lut, "tex_in", -1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	// Undo the disconnect_ex so the undo/redo sequence below starts from
	// the same "connected" state as before this block.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 1);
	EXPECT_TRUE(oakengine_node_disconnect(lut, "tex_in") == OAKENGINE_OK);

	// Undo/redo the disconnect and the connect: undo brings the connection
	// back, undo again removes it; redoing both replays connect then
	// disconnect, so the end state is disconnected.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 1);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 0);
}

static void test_remove(OakEngineProject *project, OakEngineNode *solid,
						OakEngineNode *lut)
{
	const int before = oakengine_project_node_count(project);

	// A node from another project is refused.
	OakEngineProject *other = oakengine_project_create();
	EXPECT_TRUE(other != NULL);
	EXPECT_TRUE(oakengine_project_new(other) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(other, solid) ==
		   OAKENGINE_E_INVALID);
	oakengine_project_free(other);

	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_node_count(project) == before - 1);

	// Undo brings the node back, redo removes it again.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_node_count(project) == before);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_node_count(project) == before - 1);

	(void)solid;
}

static void test_label_and_color_many(OakEngineProject *project)
{
	OakEngineNode *a = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *b = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.text3");
	EXPECT_TRUE(a != NULL && b != NULL);
	OakEngineNode *two[2] = { a, b };
	char buf[128];

	// One command renames both; undo restores each node's own label.
	EXPECT_TRUE(oakengine_node_set_label_many(two, 2, "Shared") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_label(a, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "Shared") == 0);
	EXPECT_TRUE(oakengine_node_get_label(b, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "Shared") == 0);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	// The original labels were empty.
	EXPECT_TRUE(oakengine_node_get_label(a, buf, sizeof(buf)) == 0);
	EXPECT_TRUE(oakengine_node_get_label(b, buf, sizeof(buf)) == 0);
	EXPECT_TRUE(oakengine_node_set_label_many(NULL, 1, "x") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_label_many(two, 0, "x") == OAKENGINE_OK);

	// Color labels batch in one command too.
	EXPECT_TRUE(oakengine_node_get_color_label(a) == -1);
	EXPECT_TRUE(oakengine_node_set_color_label(two, 2, 5) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_color_label(a) == 5);
	EXPECT_TRUE(oakengine_node_get_color_label(b) == 5);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_color_label(a) == -1);
	EXPECT_TRUE(oakengine_node_get_color_label(b) == -1);
	EXPECT_TRUE(oakengine_node_get_color_label(NULL) == -1);
	EXPECT_TRUE(oakengine_node_set_color_label(NULL, 1, 1) ==
		   OAKENGINE_E_INVALID);
}

// Extended metadata and value-at-time family (B8a): input introspection,
// properties, label/input names, defaults, project/edge lookup,
// copy_inputs and the at-time value readers.
static void test_extended_metadata(OakEngineProject *project)
{
	char buf[256];

	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	OakEngineNode *text = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.text3");
	EXPECT_TRUE(solid != NULL && lut != NULL && text != NULL);

	// Introspection.
	EXPECT_TRUE(oakengine_node_input_is_array(text, "args_in") == 1);
	EXPECT_TRUE(oakengine_node_input_is_array(solid, "color_in") == 0);
	EXPECT_TRUE(oakengine_node_input_array_size(text, "args_in") >= 0);
	EXPECT_TRUE(oakengine_node_input_array_size(solid, "color_in") == 0);
	EXPECT_TRUE(oakengine_node_input_get_flags(solid, "color_in") >= 0);
	EXPECT_TRUE(oakengine_node_input_get_flags(NULL, "color_in") == 0);
	EXPECT_TRUE(oakengine_node_input_is_connectable(lut, "tex_in") == 1);
	EXPECT_TRUE(oakengine_node_input_is_connectable(lut, "lut_file_in") == 0);
	EXPECT_TRUE(oakengine_node_input_is_keyframable(solid, "color_in") == 1);
	EXPECT_TRUE(oakengine_node_input_is_keyframable(lut, "tex_in") == 0);
	EXPECT_TRUE(oakengine_node_input_is_keyframed_ex(solid, "color_in", -1) == 0);

	// Properties: set (with and without notification), read back through
	// every typed getter, enumerate.
	EXPECT_TRUE(oakengine_node_input_has_property(solid, "color_in",
											 "my_prop") == 0);
	EXPECT_TRUE(oakengine_node_set_input_property_string(
			   solid, "color_in", "my_prop", "2.5", 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_has_property(solid, "color_in",
											 "my_prop") == 1);
	EXPECT_TRUE(oakengine_node_input_get_property_string(
			   solid, "color_in", "my_prop", buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "2.5") == 0);
	double d = 0;
	EXPECT_TRUE(oakengine_node_input_get_property_number(solid, "color_in",
													"my_prop", -1, &d) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(d - 2.5) < 1e-9);
	// The per-track variant resolves (component value is type-dependent).
	EXPECT_TRUE(oakengine_node_input_get_property_number(solid, "color_in",
													"my_prop", 2, &d) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_set_input_property_string(
			   solid, "color_in", "int_prop", "7", 1) == OAKENGINE_OK);
	int64_t i64 = 0;
	EXPECT_TRUE(oakengine_node_input_get_property_int(solid, "color_in",
												 "int_prop", &i64) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(i64 == 7);
	EXPECT_TRUE(oakengine_node_input_get_property_rational(
			   solid, "color_in", "my_prop", NULL, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_get_property_count(solid, "color_in") >= 1);
	EXPECT_TRUE(oakengine_node_input_get_property_string(
			   solid, "color_in", "no_such", buf, sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_input_get_property_number(solid, "color_in",
													"no_such", -1, &d) ==
		   OAKENGINE_E_NOT_FOUND);
	// A scalar string reads back as a one-element list.
	EXPECT_TRUE(oakengine_node_input_get_property_string_list_count(
			   solid, "color_in", "my_prop") == 1);
	EXPECT_TRUE(oakengine_node_input_get_property_string_list(
			   solid, "color_in", "my_prop", 0, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "2.5") == 0);
	EXPECT_TRUE(oakengine_node_input_get_property_string_list(
			   solid, "color_in", "my_prop", 1, buf, sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	// Suppressed write keeps the value too.
	EXPECT_TRUE(oakengine_node_set_input_property_string(
			   solid, "color_in", "my_prop", "3.5", 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_get_property_string(
			   solid, "color_in", "my_prop", buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "3.5") == 0);

	// Names.
	EXPECT_TRUE(oakengine_node_get_label_and_name(solid, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "Solid") == 0);
	EXPECT_TRUE(oakengine_node_set_label(solid, "MySolid") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_label_and_name(solid, buf, sizeof(buf)) > 0);
	EXPECT_TRUE(strstr(buf, "MySolid") != NULL && strstr(buf, "Solid") != NULL);
	EXPECT_TRUE(oakengine_node_get_input_name(solid, "color_in", buf,
										 sizeof(buf)) >= 0);

	// Default value: Solid's color defaults to opaque red.
	oak_node_value def;
	EXPECT_TRUE(oakengine_node_input_get_default_value(solid, "color_in", 0,
												  &def) == OAKENGINE_OK);
	EXPECT_TRUE(def.type == OAK_NODE_VALUE_COLOR && fabs(def.f[0] - 1.0) < 1e-6);
	EXPECT_TRUE(oakengine_node_input_get_default_value(solid, "color_in", 99,
												  &def) == OAKENGINE_E_NOT_FOUND);

	// Project and edge lookup.
	EXPECT_TRUE(oakengine_node_get_project(solid) == project);
	EXPECT_TRUE(oakengine_node_get_project(NULL) == NULL);
	EXPECT_TRUE(oakengine_node_input_get_connected_node(lut, "tex_in", -1) ==
		   NULL);
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_get_connected_node(lut, "tex_in", -1) ==
		   solid);
	EXPECT_TRUE(oakengine_node_disconnect(lut, "tex_in") == OAKENGINE_OK);

	// copy_inputs: values (not connections) transfer as one undoable step.
	OakEngineNode *solid2 = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid2 != NULL);
	oak_node_value c;
	memset(&c, 0, sizeof(c));
	c.type = OAK_NODE_VALUE_COLOR;
	c.f[0] = 0.1;
	c.f[1] = 0.2;
	c.f[2] = 0.3;
	c.f[3] = 1.0;
	EXPECT_TRUE(oakengine_node_set_input(solid, "color_in", &c) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_copy_inputs(solid2, solid) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input(solid2, "color_in", &def) == OAKENGINE_OK);
	EXPECT_TRUE(fabs(def.f[0] - 0.1) < 1e-6 && fabs(def.f[2] - 0.3) < 1e-6);
	EXPECT_TRUE(oakengine_node_copy_inputs(NULL, solid) == OAKENGINE_E_INVALID);

	// At-time readers: whole value and per-track component.
	oak_node_value at;
	EXPECT_TRUE(oakengine_node_get_input_at_time(solid, "color_in", -1, -1, 0, 1,
											&at) == OAKENGINE_OK);
	EXPECT_TRUE(at.type == OAK_NODE_VALUE_COLOR && fabs(at.f[0] - 0.1) < 1e-6);
	EXPECT_TRUE(oakengine_node_get_input_at_time(solid, "color_in", -1, 2, 0, 1,
											&at) == OAKENGINE_OK);
	EXPECT_TRUE(at.type == OAK_NODE_VALUE_COLOR && fabs(at.f[0] - 0.3) < 1e-6);
	EXPECT_TRUE(oakengine_node_get_input_at_time(solid, "enabled_in", -1, 0, 0,
											1, &at) == OAKENGINE_OK);
	EXPECT_TRUE(at.type == OAK_NODE_VALUE_BOOL && at.num == 1);
	// String-family inputs need the string getter.
	EXPECT_TRUE(oakengine_node_get_input_at_time(text, "text_in", -1, 0, 0, 1,
											&at) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_input_string_at_time(text, "text_in", -1, 0,
												   "hello") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_input_string_at_time(text, "text_in", -1, 0,
												   1, buf,
												   sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "hello") == 0);
	// Bezier/binary getters reject mismatched inputs.
	double b6[6];
	EXPECT_TRUE(oakengine_node_get_input_bezier_at_time(solid, "color_in", -1, 0,
												   1, b6) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_get_input_binary_at_time(solid, "color_in", -1, 0,
												   1, NULL, 0) ==
		   OAKENGINE_E_INVALID);

	// Clean up the played-with nodes so later tests see a fresh graph.
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid2) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, text) == OAKENGINE_OK);
}

// ---- Context positions -----------------------------------------------------

static void test_context_positions(OakEngineProject *project)
{
	OakEngineNode *group = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.group");
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	EXPECT_TRUE(group != NULL && solid != NULL && lut != NULL);

	double x = 0, y = 0;
	int expanded = -1;

	// NULL safety.
	EXPECT_TRUE(oakengine_node_context_contains_node(NULL, solid) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_context_node_count(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_context_node_at(NULL, 0, NULL, NULL, NULL) == NULL);
	EXPECT_TRUE(oakengine_node_set_context_position(NULL, solid, 0, 0) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_context_expanded(NULL, solid, 1) ==
		   OAKENGINE_E_INVALID);

	// A fresh group context is empty.
	EXPECT_TRUE(oakengine_node_context_contains_node(group, solid) == 0);
	EXPECT_TRUE(oakengine_node_context_node_count(group) == 0);
	EXPECT_TRUE(oakengine_node_get_context_position(group, solid, &x, &y,
											   &expanded) ==
		   OAKENGINE_E_NOT_FOUND);

	// set_context_position inserts like the C++ setter.
	EXPECT_TRUE(oakengine_node_set_context_position(group, solid, 3.5, -2.0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_context_contains_node(group, solid) == 1);
	EXPECT_TRUE(oakengine_node_context_node_count(group) == 1);
	EXPECT_TRUE(oakengine_node_get_context_position(group, solid, &x, &y,
											   &expanded) == OAKENGINE_OK);
	EXPECT_TRUE(x == 3.5 && y == -2.0 && expanded == 0);

	// Expanded flag round-trips.
	EXPECT_TRUE(oakengine_node_set_context_expanded(group, solid, 1) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_context_position(group, solid, &x, &y,
											   &expanded) == OAKENGINE_OK);
	EXPECT_TRUE(expanded == 1);

	// Moving keeps the expanded flag.
	EXPECT_TRUE(oakengine_node_set_context_position(group, solid, 1.0, 2.0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_get_context_position(group, solid, &x, &y,
											   &expanded) == OAKENGINE_OK);
	EXPECT_TRUE(x == 1.0 && y == 2.0 && expanded == 1);

	EXPECT_TRUE(oakengine_node_set_context_position(group, lut, -4.0, 5.0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_context_node_count(group) == 2);

	// Enumeration (order is the hash map's; find both by handle).
	OakEngineNode *seen0 = oakengine_node_context_node_at(group, 0, &x, &y,
														  &expanded);
	OakEngineNode *seen1 = oakengine_node_context_node_at(group, 1, NULL,
														  NULL, NULL);
	EXPECT_TRUE(seen0 != NULL && seen1 != NULL && seen0 != seen1);
	EXPECT_TRUE((seen0 == solid || seen0 == lut) &&
		   (seen1 == solid || seen1 == lut));
	EXPECT_TRUE(oakengine_node_context_node_at(group, 2, NULL, NULL, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_node_context_node_at(group, -1, NULL, NULL, NULL) ==
		   NULL);

	EXPECT_TRUE(oakengine_project_remove_node(project, group) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
}

// ---- Effect input ------------------------------------------------------------

static void test_get_effect_input(OakEngineProject *project)
{
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(lut != NULL && solid != NULL);

	char buf[64];
	int element = 99;

	EXPECT_TRUE(oakengine_node_get_effect_input(NULL, buf, sizeof(buf),
										   &element) == OAKENGINE_E_INVALID);

	// OCIO LUT declares its texture input as the effect input.
	EXPECT_TRUE(oakengine_node_get_effect_input(lut, buf, sizeof(buf),
										   &element) >= 0);
	EXPECT_TRUE(strcmp(buf, "tex_in") == 0 && element == -1);

	// The solid generator has no effect input.
	EXPECT_TRUE(oakengine_node_get_effect_input(solid, buf, sizeof(buf),
										   &element) ==
		   OAKENGINE_E_NOT_FOUND);

	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
}

// ---- Group nodes -------------------------------------------------------------

static void test_group(OakEngineProject *project)
{
	OakEngineNode *group = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.group");
	OakEngineNode *group2 = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.group");
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	EXPECT_TRUE(group != NULL && group2 != NULL && solid != NULL && lut != NULL);

	// Type probe.
	EXPECT_TRUE(oakengine_node_is_group(group) == 1);
	EXPECT_TRUE(oakengine_node_is_group(solid) == 0);
	EXPECT_TRUE(oakengine_node_is_group(NULL) == 0);
	EXPECT_TRUE(oakengine_group_input_passthrough_count(solid) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_group_add_input_passthrough(solid, lut, "x", -1, NULL,
												 NULL, 0) ==
		   OAKENGINE_E_INVALID);

	// Direct passthrough add: the generated id is returned. The group must
	// contain the inner node first (NodeGroup::add_input_passthrough
	// asserts context membership).
	EXPECT_TRUE(oakengine_node_set_context_position(group, solid, 0, 0) ==
		   OAKENGINE_OK);
	char idbuf[64];
	EXPECT_TRUE(oakengine_group_add_input_passthrough(group, solid, "color_in",
												 -1, NULL, idbuf,
												 sizeof(idbuf)) > 0);
	EXPECT_TRUE(idbuf[0] != '\0');
	EXPECT_TRUE(oakengine_group_input_passthrough_count(group) == 1);

	// Read back the passthrough.
	char id_at[64], input_at[64];
	OakEngineNode *node_at = NULL;
	int element_at = 99;
	EXPECT_TRUE(oakengine_group_input_passthrough_at(group, 0, id_at,
												sizeof(id_at), &node_at,
												input_at, sizeof(input_at),
												&element_at) > 0);
	EXPECT_TRUE(strcmp(id_at, idbuf) == 0 && node_at == solid &&
		   strcmp(input_at, "color_in") == 0 && element_at == -1);
	EXPECT_TRUE(oakengine_group_input_passthrough_at(group, 1, NULL, 0, NULL,
												NULL, 0, NULL) ==
		   OAKENGINE_E_INVALID);

	// Id lookup by (node, input, element).
	char idq[64];
	EXPECT_TRUE(oakengine_group_get_id_of_passthrough(group, solid, "color_in",
												 -1, idq, sizeof(idq)) > 0);
	EXPECT_TRUE(strcmp(idq, idbuf) == 0);
	EXPECT_TRUE(oakengine_group_get_id_of_passthrough(group, lut, "tex_in", -1,
												 idq, sizeof(idq)) ==
		   OAKENGINE_E_NOT_FOUND);

	// Output passthrough (direct variant).
	EXPECT_TRUE(oakengine_group_get_output_passthrough(group) == NULL);
	EXPECT_TRUE(oakengine_group_set_output_passthrough(group, solid) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_get_output_passthrough(group) == solid);

	// Resolve one level: (group, idbuf) -> (solid, color_in).
	OakEngineNode *resolved_node = NULL;
	char resolved_input[64];
	int resolved_element = 99;
	EXPECT_TRUE(oakengine_group_resolve_input(group, idbuf, -1, &resolved_node,
										 resolved_input,
										 sizeof(resolved_input),
										 &resolved_element) >= 0);
	EXPECT_TRUE(resolved_node == solid && strcmp(resolved_input, "color_in") == 0);

	// Resolving a plain node input passes through unchanged.
	EXPECT_TRUE(oakengine_group_resolve_input(solid, "color_in", -1,
										 &resolved_node, resolved_input,
										 sizeof(resolved_input),
										 &resolved_element) >= 0);
	EXPECT_TRUE(resolved_node == solid && strcmp(resolved_input, "color_in") == 0);

	// Nested groups resolve to the innermost real input.
	char id2[64];
	EXPECT_TRUE(oakengine_node_set_context_position(group2, group, 0, 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_add_input_passthrough(group2, group, idbuf, -1,
												 NULL, id2, sizeof(id2)) > 0);
	EXPECT_TRUE(oakengine_group_resolve_input(group2, id2, -1, &resolved_node,
										 resolved_input,
										 sizeof(resolved_input),
										 &resolved_element) >= 0);
	EXPECT_TRUE(resolved_node == solid && strcmp(resolved_input, "color_in") == 0);

	// Direct remove.
	EXPECT_TRUE(oakengine_group_remove_input_passthrough(group, solid, "color_in",
													-1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_input_passthrough_count(group) == 0);
	EXPECT_TRUE(oakengine_group_remove_input_passthrough(group, solid, "color_in",
													-1) ==
		   OAKENGINE_E_NOT_FOUND);

	// Undoable add: one command on the project undo stack.
	EXPECT_TRUE(oakengine_node_set_context_position(group, lut, 0, 0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_add_input_passthrough_undoable(group, lut,
														  "tex_in", -1,
														  NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_input_passthrough_count(group) == 1);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_input_passthrough_count(group) == 0);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_input_passthrough_count(group) == 1);

	// Undoable output passthrough.
	EXPECT_TRUE(oakengine_group_set_output_passthrough_undoable(group, lut) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_get_output_passthrough(group) == lut);
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_get_output_passthrough(group) == solid);
	EXPECT_TRUE(oakengine_project_redo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_group_get_output_passthrough(group) == lut);

	EXPECT_TRUE(oakengine_project_remove_node(project, group) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, group2) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
}

// ---- Multi-camera nodes --------------------------------------------------------

static void test_multicam(OakEngineProject *project)
{
	OakEngineNode *cam = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.multicam");
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(cam != NULL && solid != NULL);

	// Type probe.
	EXPECT_TRUE(oakengine_node_is_multicam(cam) == 1);
	EXPECT_TRUE(oakengine_node_is_multicam(solid) == 0);
	EXPECT_TRUE(oakengine_node_is_multicam(NULL) == 0);

	// Input id constants.
	const char *cur = oakengine_multicam_input_current();
	const char *src = oakengine_multicam_input_sources();
	const char *seq = oakengine_multicam_input_sequence();
	const char *seqt = oakengine_multicam_input_sequence_type();
	EXPECT_TRUE(cur != NULL && src != NULL && seq != NULL && seqt != NULL);
	EXPECT_TRUE(strcmp(cur, "current_in") == 0);
	EXPECT_TRUE(strcmp(src, "sources_in") == 0);
	EXPECT_TRUE(strcmp(seq, "sequence_in") == 0);
	EXPECT_TRUE(strcmp(seqt, "sequence_type_in") == 0);

	// A fresh multicam has no connected sources.
	EXPECT_TRUE(oakengine_multicam_get_source_count(cam) == 0);
	EXPECT_TRUE(oakengine_multicam_get_source_count(solid) ==
		   OAKENGINE_E_INVALID);

	// Grid layout math (static, no node needed).
	int rows = 0, cols = 0;
	EXPECT_TRUE(oakengine_multicam_get_rows_and_columns(-1, &rows, &cols) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_multicam_get_rows_and_columns(1, &rows, &cols) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(rows == 1 && cols == 1);
	EXPECT_TRUE(oakengine_multicam_get_rows_and_columns(2, &rows, &cols) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(rows == 1 && cols == 2);
	EXPECT_TRUE(oakengine_multicam_get_rows_and_columns(4, &rows, &cols) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(rows == 2 && cols == 2);
	EXPECT_TRUE(oakengine_multicam_get_rows_and_columns(5, &rows, &cols) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(rows == 2 && cols == 3);

	// index <-> (row, col) is an inverse pair for every tile.
	for (int sources = 1; sources <= 9; sources++) {
		EXPECT_TRUE(oakengine_multicam_get_rows_and_columns(sources, &rows,
													   &cols) ==
			   OAKENGINE_OK);
		for (int index = 0; index < sources; index++) {
			int row = -1, col = -1;
			EXPECT_TRUE(oakengine_multicam_index_to_row_cols(index, rows, cols,
														&row, &col) ==
				   OAKENGINE_OK);
			EXPECT_TRUE(row >= 0 && row < rows && col >= 0 && col < cols);
			EXPECT_TRUE(oakengine_multicam_rows_cols_to_index(row, col, rows,
														 cols) == index);
		}
	}
	EXPECT_TRUE(oakengine_multicam_index_to_row_cols(-1, 1, 1, &rows, &cols) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_multicam_rows_cols_to_index(-1, 0, 1, 1) ==
		   OAKENGINE_E_INVALID);

	EXPECT_TRUE(oakengine_project_remove_node(project, cam) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
}

// ---- Bulk graph deletion ---------------------------------------------------

static void test_nodes_delete_many(OakEngineProject *project)
{
	// A group acts as the node-view context (the project itself is not a
	// node).
	OakEngineNode *context = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.group");
	EXPECT_TRUE(context != NULL);

	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	EXPECT_TRUE(solid != NULL && lut != NULL);
	EXPECT_TRUE(oakengine_node_connect(solid, lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_set_context_position(context, solid, 1.0, 2.0) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_set_context_position(context, lut, 3.0, 4.0) ==
		   OAKENGINE_OK);

	// Argument validation.
	EXPECT_TRUE(oakengine_nodes_delete_many(NULL, NULL, 1, NULL, NULL, NULL,
									   NULL, 0) == OAKENGINE_E_INVALID);

	const int before = oakengine_project_node_count(project);

	OakEngineNode *nodes[2] = { solid, lut };
	OakEngineNode *contexts[2] = { context, context };
	OakEngineNode *edge_outputs[1] = { solid };
	OakEngineNode *edge_input_nodes[1] = { lut };
	const char *edge_input_ids[1] = { "tex_in" };
	int edge_input_elements[1] = { -1 };
	EXPECT_TRUE(oakengine_nodes_delete_many(nodes, contexts, 2, edge_outputs,
									   edge_input_nodes, edge_input_ids,
									   edge_input_elements,
									   1) == OAKENGINE_OK);

	// Both nodes left the graph (no other context held them) and the edge
	// is gone.
	EXPECT_TRUE(oakengine_project_node_count(project) == before - 2);
	EXPECT_TRUE(oakengine_node_context_contains_node(context, solid) == 0);
	EXPECT_TRUE(oakengine_node_context_contains_node(context, lut) == 0);

	// One undo restores the nodes, their context positions and the edge.
	EXPECT_TRUE(oakengine_project_undo(project) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_node_count(project) == before);
	EXPECT_TRUE(oakengine_node_context_contains_node(context, solid) == 1);
	EXPECT_TRUE(oakengine_node_context_contains_node(context, lut) == 1);
	double x = 0, y = 0;
	EXPECT_TRUE(oakengine_node_get_context_position(context, solid, &x, &y,
											   NULL) == OAKENGINE_OK);
	EXPECT_TRUE(x == 1.0 && y == 2.0);
	EXPECT_TRUE(oakengine_node_input_is_connected(lut, "tex_in") == 1);

	// Clean up.
	EXPECT_TRUE(oakengine_node_disconnect(lut, "tex_in") == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, lut) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_project_remove_node(project, context) == OAKENGINE_OK);
}

// ---- Node frame time base ---------------------------------------------------

static void test_node_frame_time_base(OakEngineProject *project)
{
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);

	// NULL safety.
	int num = -1, den = -1;
	EXPECT_TRUE(oakengine_node_frame_time_base(NULL, &num, &den) ==
		   OAKENGINE_E_INVALID);

	// A solid node (not on a sequence) returns a sensible default.
	EXPECT_TRUE(oakengine_node_frame_time_base(solid, NULL, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_frame_time_base(solid, &num, NULL) == OAKENGINE_OK);
	EXPECT_TRUE(num > 0);
	EXPECT_TRUE(oakengine_node_frame_time_base(solid, NULL, &den) == OAKENGINE_OK);
	EXPECT_TRUE(den > 0);
	EXPECT_TRUE(oakengine_node_frame_time_base(solid, &num, &den) == OAKENGINE_OK);
	EXPECT_TRUE(num > 0 && den > 0);

	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
}

// ---- Input property key iteration --------------------------------------------

static void test_node_input_get_property_key(OakEngineProject *project)
{
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);

	char buf[64];

	// NULL safety.
	EXPECT_TRUE(oakengine_node_input_get_property_key(NULL, "enabled_in", 0, buf,
												sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_input_get_property_key(solid, NULL, 0, buf,
												sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// Set a property, then read the key at index 0.
	EXPECT_TRUE(oakengine_node_set_input_property_string(solid, "enabled_in",
													"my_key", "my_value",
													1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_node_input_get_property_key(solid, "enabled_in", 0, buf,
												sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, "my_key") == 0);

	// Out of range index.
	EXPECT_TRUE(oakengine_node_input_get_property_key(solid, "enabled_in", 99, buf,
												sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);

	// Query length mode.
	EXPECT_TRUE(oakengine_node_input_get_property_key(solid, "enabled_in", 0, NULL,
												 0) == (int)strlen("my_key"));

	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
}

// ---- Keyframe best type at time ---------------------------------------------

static void test_node_keyframe_best_type_at_time(OakEngineProject *project)
{
	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);

	// Must not crash on NULL/invalid input.
	int type = oakengine_node_keyframe_best_type_at_time(NULL, "color_in", -1,
														 0, 0, 1);
	(void) type;

	type = oakengine_node_keyframe_best_type_at_time(solid, NULL, -1, 0, 0, 1);
	(void) type;

	// Non-keyframed input returns the default easing type (>= 0).
	type = oakengine_node_keyframe_best_type_at_time(solid, "color_in", -1,
													 0, 0, 1);
	EXPECT_TRUE(type >= 0);

	EXPECT_TRUE(oakengine_project_remove_node(project, solid) == OAKENGINE_OK);
}

static void test_misc_node_facades(OakEngineProject *project)
{
	// Subtitle text getter/setter
	OakEngineNode *sub = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.subtitle");
	EXPECT_TRUE(sub != NULL);
	EXPECT_TRUE(oakengine_subtitle_set_text(sub, "Hello subtitles") ==
		   OAKENGINE_OK);
	char buf[64];
	EXPECT_TRUE(oakengine_subtitle_get_text(sub, buf, sizeof(buf)) == 15);
	EXPECT_TRUE(strcmp(buf, "Hello subtitles") == 0);
	EXPECT_TRUE(strcmp(oakengine_subtitle_text_input_id(), "text_in") == 0);

	// Multicam current source defaults to 0
	OakEngineNode *mc = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.multicam");
	EXPECT_TRUE(mc != NULL);
	EXPECT_TRUE(oakengine_multicam_get_current_source(mc) == 0);

	// Shape rect: valid call with a dummy command should succeed.
	OakEngineNode *shape = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.shape");
	EXPECT_TRUE(shape != NULL);
	void *cmd = oakengine_undo_command_create_multi();
	oak_video_params pod = {};
	pod.width = 1920;
	pod.height = 1080;
	pod.format = 0;
	pod.divider = 1;
	EXPECT_TRUE(oakengine_shape_set_rect_undoable(shape, 0, 0, 100, 100, &pod,
										 cmd) == OAKENGINE_OK);
	oakengine_undo_command_free(cmd);
}

TEST(OakEngineNode, Main)
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

	test_enumeration(project);

	OakEngineNode *solid = NULL, *lut = NULL;
	test_add_and_metadata(project, &solid, &lut);
	test_inputs_and_params(project, solid, lut);
	test_edges(project, solid, lut);
	test_remove(project, solid, lut);
	test_label_and_color_many(project);
	test_extended_metadata(project);
	test_context_positions(project);
	test_get_effect_input(project);
	test_group(project);
	test_multicam(project);
	test_nodes_delete_many(project);
	test_node_frame_time_base(project);
	test_node_input_get_property_key(project);
	test_node_keyframe_best_type_at_time(project);
	test_misc_node_facades(project);

	// Graph nodes are not timeline clips: a sequence's track list stays
	// empty no matter what the project graph holds.
	OakEngineSequence *seq = oakengine_sequence_new(project, "Seq");
	EXPECT_TRUE(seq != NULL);
	int video = -1, audio = -1, subtitle = -1;
	EXPECT_TRUE(oakengine_sequence_track_count(seq, &video, &audio, &subtitle) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(video == 0 && audio == 0 && subtitle == 0);

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
