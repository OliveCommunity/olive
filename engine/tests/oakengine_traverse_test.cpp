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

// Pure C ABI test for the liboakengine traverse facade
// (oakengine/traverse.h) plus the node value-hint write path
// (oakengine_node_set_value_hint()). Builds a small node graph with the
// facade node family and exercises generate_database/generate_table, the db
// accessors, element_index_for_hint, generate_row's C-side error paths and
// transform. No GL required: evaluation is synchronous and CPU-only
// (textures resolve as engine-side dummy textures), so no GL gating is
// needed.

#include <assert.h>
#include <gtest/gtest.h>
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
#include "oakengine/traverse.h"

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	EXPECT_TRUE(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_traverse_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_traverse_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

// ---- Robustness: NULL/invalid arguments ------------------------------------

static void test_null_robustness(OakEngineNode *solid)
{
	double m[6];

	EXPECT_TRUE(oakengine_traverse_generate_database(NULL, 0, 1, 1, 1) == NULL);
	EXPECT_TRUE(oakengine_traverse_generate_table(NULL, 0, 1, 1, 1) == NULL);
	// Zero denominators are invalid rationals.
	EXPECT_TRUE(oakengine_traverse_generate_database(solid, 0, 0, 1, 1) == NULL);
	EXPECT_TRUE(oakengine_traverse_generate_table(solid, 0, 1, 1, 0) == NULL);

	oakengine_traverse_db_free(NULL); // no-op

	EXPECT_TRUE(oakengine_traverse_db_input_count(NULL) == 0);
	EXPECT_TRUE(oakengine_traverse_db_input_id(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_traverse_db_row_count(NULL, 0) == 0);
	EXPECT_TRUE(oakengine_traverse_row_type(NULL, 0, 0) == OAK_NODE_VALUE_NONE);
	EXPECT_TRUE(oakengine_traverse_row_source(NULL, 0, 0) == NULL);
	EXPECT_TRUE(oakengine_traverse_row_tag(NULL, 0, 0) != NULL); // never NULL
	EXPECT_TRUE(oakengine_traverse_row_value_string(NULL, 0, 0) == NULL);
	EXPECT_TRUE(oakengine_traverse_row_split_count(NULL, 0, 0) == 0);
	EXPECT_TRUE(oakengine_traverse_row_split_string(NULL, 0, 0, 0) == NULL);

	EXPECT_TRUE(oakengine_traverse_table_element_index_for_hint(NULL, "x", -1,
														   NULL) == -1);

	// generate_row: the C side can only exercise the error paths -- the
	// real output is an olive::NodeValueRow (a C++ QHash typedef), which a
	// pure C test cannot allocate. The filled-row path is covered by the
	// application (the viewer display gizmo drag-start path).
	EXPECT_TRUE(oakengine_traverse_generate_row(NULL, 0, 1, 1, 1, NULL, 0, 0,
										   (void *)1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_traverse_generate_row(solid, 0, 1, 1, 1, NULL, 0, 0,
										   NULL) == OAKENGINE_E_INVALID);

	EXPECT_TRUE(oakengine_traverse_transform(NULL, solid, 0, 1, 1, 1, NULL, m) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_traverse_transform(solid, NULL, 0, 1, 1, 1, NULL, m) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_traverse_transform(solid, solid, 0, 1, 1, 1, NULL,
										NULL) == OAKENGINE_E_INVALID);

	EXPECT_TRUE(oakengine_node_set_value_hint(NULL, "x", -1, OAK_NODE_VALUE_COLOR,
										 0, NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_node_set_value_hint(solid, NULL, -1,
										 OAK_NODE_VALUE_COLOR, 0,
										 NULL) == OAKENGINE_E_INVALID);
}

// ---- generate_database + accessors ------------------------------------------

static void test_database(OakEngineNode *solid)
{
	char buf[256];

	OakEngineTraverseDb *db =
		oakengine_traverse_generate_database(solid, 0, 1, 1, 1);
	EXPECT_TRUE(db != NULL);

	// One entry per node input, in the node's input order (deterministic).
	const int node_inputs = oakengine_node_input_count(solid);
	EXPECT_TRUE(node_inputs >= 2);
	EXPECT_TRUE(oakengine_traverse_db_input_count(db) == node_inputs);
	for (int i = 0; i < node_inputs; i++) {
		EXPECT_TRUE(oakengine_node_input_id(solid, i, buf, sizeof(buf)) > 0);
		const char *id = oakengine_traverse_db_input_id(db, i);
		EXPECT_TRUE(id != NULL);
		EXPECT_TRUE(strcmp(id, buf) == 0);

		// Every input of an unconnected Solid generator produces one row
		// (its standard value).
		const int rows = oakengine_traverse_db_row_count(db, i);
		EXPECT_TRUE(rows >= 1);
		for (int r = 0; r < rows; r++) {
			// Type is a valid facade value type for plain inputs
			// (enabled_in is BOOL, color_in is COLOR).
			const int type = oakengine_traverse_row_type(db, i, r);
			EXPECT_TRUE(type > OAK_NODE_VALUE_NONE);
			// Source: the value's originating node, or NULL; the solid's
			// standard values are sourced from the node itself.
			OakEngineNode *src = oakengine_traverse_row_source(db, i, r);
			EXPECT_TRUE(src == NULL || src == solid);
			// Tag may be empty but never NULL.
			EXPECT_TRUE(oakengine_traverse_row_tag(db, i, r) != NULL);
			// Value string is non-empty for these value types.
			const char *vs = oakengine_traverse_row_value_string(db, i, r);
			EXPECT_TRUE(vs != NULL);
			EXPECT_TRUE(vs[0] != '\0');
			// Split values: at least one track, each with a string.
			const int splits = oakengine_traverse_row_split_count(db, i, r);
			EXPECT_TRUE(splits >= 1);
			for (int s = 0; s < splits; s++) {
				EXPECT_TRUE(oakengine_traverse_row_split_string(db, i, r, s) !=
					   NULL);
			}
			EXPECT_TRUE(oakengine_traverse_row_split_string(db, i, r, splits) ==
				   NULL);
		}
	}

	// Out-of-range accessors fail cleanly.
	EXPECT_TRUE(oakengine_traverse_db_input_id(db, node_inputs) == NULL);
	EXPECT_TRUE(oakengine_traverse_db_row_count(db, node_inputs) == 0);
	EXPECT_TRUE(oakengine_traverse_row_type(db, node_inputs, 0) ==
		   OAK_NODE_VALUE_NONE);

	// A multi-entry database is not a generate_table result: the hint
	// lookup rejects it.
	EXPECT_TRUE(oakengine_traverse_table_element_index_for_hint(solid, "color_in",
														   -1, db) == -1);

	oakengine_traverse_db_free(db);
}

// ---- generate_table + element_index_for_hint + set_value_hint -----------------

static void test_table_and_hints(OakEngineNode *solid, OakEngineNode *lut)
{
	OakEngineTraverseDb *db =
		oakengine_traverse_generate_table(solid, 0, 1, 1, 1);
	EXPECT_TRUE(db != NULL);
	EXPECT_TRUE(oakengine_traverse_db_input_count(db) == 1);
	// The single output table is keyed by an empty input id.
	const char *id = oakengine_traverse_db_input_id(db, 0);
	EXPECT_TRUE(id != NULL);
	EXPECT_TRUE(id[0] == '\0');
	EXPECT_TRUE(oakengine_traverse_db_row_count(db, 0) >= 1);

	// set_value_hint: unknown input ids and bogus types are rejected.
	EXPECT_TRUE(oakengine_node_set_value_hint(solid, "not_an_input", -1,
										 OAK_NODE_VALUE_COLOR, 0,
										 NULL) == OAKENGINE_E_NOT_FOUND);
	EXPECT_TRUE(oakengine_node_set_value_hint(solid, "color_in", -1, 999, 0,
										 NULL) == OAKENGINE_E_INVALID);

	// The solid's output table holds texture rows. A hint preferring COLOR
	// values (set on the lut's texture input) matches nothing -> -1.
	EXPECT_TRUE(oakengine_node_set_value_hint(lut, "tex_in", -1,
										 OAK_NODE_VALUE_COLOR, -1,
										 NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_traverse_table_element_index_for_hint(lut, "tex_in", -1,
														   db) == -1);

	// An untyped hint falls back to the input's declared type (k_texture
	// for "tex_in"), which does have a row in the table.
	EXPECT_TRUE(oakengine_node_set_value_hint(lut, "tex_in", -1,
										 OAK_NODE_VALUE_NONE, -1,
										 NULL) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_traverse_table_element_index_for_hint(lut, "tex_in", -1,
														   db) >= 0);

	oakengine_traverse_db_free(db);
}

// ---- transform ------------------------------------------------------------------

static void test_transform(OakEngineNode *solid, OakEngineNode *lut)
{
	double m[6] = { 0, 0, 0, 0, 0, 0 };

	// No transform-generating nodes between start and end: identity matrix.
	EXPECT_TRUE(oakengine_traverse_transform(solid, solid, 0, 1, 1, 1, NULL, m) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(m[0] == 1.0 && m[1] == 0.0 && m[2] == 0.0 && m[3] == 1.0);
	EXPECT_TRUE(m[4] == 0.0 && m[5] == 0.0);

	// Same through an edge, with explicit cache params.
	oak_video_params vp;
	memset(&vp, 0, sizeof(vp));
	EXPECT_TRUE(oakengine_video_params_make(&vp, 1920, 1080, 1001, 30000, 0, 1, 1,
									   0, 0, 1) == OAKENGINE_OK);
	memset(m, 0, sizeof(m));
	EXPECT_TRUE(oakengine_traverse_transform(solid, lut, 0, 1, 1, 1, &vp, m) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(m[0] == 1.0 && m[1] == 0.0 && m[2] == 0.0 && m[3] == 1.0);
	EXPECT_TRUE(m[4] == 0.0 && m[5] == 0.0);
}

TEST(OakEngineTraverse, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations.
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

	OakEngineNode *solid = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.solidgenerator");
	EXPECT_TRUE(solid != NULL);
	OakEngineNode *lut = oakengine_project_add_node(
		project, "org.olivevideoeditor.Olive.ociolut");
	EXPECT_TRUE(lut != NULL);

	test_null_robustness(solid);
	test_database(solid);
	test_table_and_hints(solid, lut);
	test_transform(solid, lut);

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
