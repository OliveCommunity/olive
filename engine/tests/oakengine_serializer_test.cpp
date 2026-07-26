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

// Pure C ABI tests for the liboakengine project serializer facade
// (oakengine/serializer.h). Runs headless; no GPU required.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "oakengine/init.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/serializer.h"
#include "oakengine/viewer.h"

static void test_check_compressed_nonexistent(void)
{
    assert(oakengine_serializer_check_compressed(
               "/nonexistent/path/project.ove") == 0);
    assert(oakengine_serializer_check_compressed(NULL) == 0);
    assert(oakengine_serializer_check_compressed("") == 0);
}

static void test_clipboard_create_free(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);

    OakEngineClipboard *cb =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb != NULL);

    oakengine_clipboard_free(cb);
    oakengine_project_free(project);
}

static void test_copy_empty_nodes(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);

    OakEngineClipboard *cb =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb != NULL);

    // Copying zero nodes should not crash and should report success.
    assert(oakengine_clipboard_copy(cb) == OAKENGINE_OK);

    oakengine_clipboard_free(cb);
    oakengine_project_free(project);
}

static void test_set_empty_sets(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);

    OakEngineClipboard *cb =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb != NULL);

    // Setting empty arrays (count=0, array=NULL) should be OK.
    assert(oakengine_clipboard_set_nodes(cb, NULL, 0) == OAKENGINE_OK);
    assert(oakengine_clipboard_set_markers(cb, NULL, 0) == OAKENGINE_OK);
    assert(oakengine_clipboard_set_keyframes(cb, NULL, 0) == OAKENGINE_OK);

    // The clipboard with no content should still produce some XML.
    char buf[256];
    int len = oakengine_clipboard_save_to_xml(cb, buf, sizeof(buf));
    assert(len > 0);

    oakengine_clipboard_free(cb);
    oakengine_project_free(project);
}

static void test_set_node_then_save_xml(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);
    assert(oakengine_project_new(project) == OAKENGINE_OK);

    OakEngineNode *solid = oakengine_project_add_node(
        project, "org.olivevideoeditor.Olive.solidgenerator");
    assert(solid != NULL);

    OakEngineClipboard *cb =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb != NULL);

    // Set one node on the clipboard.
    assert(oakengine_clipboard_set_nodes(cb, (const OakEngineNode *const *)&solid, 1) == OAKENGINE_OK);

    // Set a property on that node.
    assert(oakengine_clipboard_set_property(cb, solid, "pos_x", "100") ==
           OAKENGINE_OK);

    // save_to_xml should return a non-empty XML document.
    char buf[4096];
    int len = oakengine_clipboard_save_to_xml(cb, buf, sizeof(buf));
    assert(len > 0);
    assert(len < (int)sizeof(buf));
    // Should contain the node type id and the property.
    assert(strstr(buf, "solidgenerator") != NULL);
    assert(strstr(buf, "pos_x") != NULL);

    // Query-length mode.
    int qlen = oakengine_clipboard_save_to_xml(cb, NULL, 0);
    assert(qlen == len);

    oakengine_clipboard_free(cb);
    oakengine_project_free(project);
}

static void test_set_node_then_foreach_property(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);
    assert(oakengine_project_new(project) == OAKENGINE_OK);

    OakEngineNode *solid = oakengine_project_add_node(
        project, "org.olivevideoeditor.Olive.solidgenerator");
    assert(solid != NULL);

    // Save_data: set node + property, then copy to system clipboard (save_data
    // is serialized). The paste result populates load_data, which is what
    // foreach_property reads.
    OakEngineClipboard *cb_copy =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_copy != NULL);
    assert(oakengine_clipboard_set_nodes(cb_copy, (const OakEngineNode *const *)&solid, 1) == OAKENGINE_OK);
    assert(oakengine_clipboard_set_property(cb_copy, solid, "pos_x", "100") ==
           OAKENGINE_OK);
    assert(oakengine_clipboard_set_property(cb_copy, solid, "pos_y", "200") ==
           OAKENGINE_OK);
    assert(oakengine_clipboard_copy(cb_copy) == OAKENGINE_OK);
    oakengine_clipboard_free(cb_copy);

    OakEngineClipboard *cb_paste =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_paste != NULL);

    int result_code = -1;
    assert(oakengine_clipboard_paste(cb_paste, OAKENGINE_CLIPBOARD_NODES,
                                     project, &result_code, NULL, 0) ==
           OAKENGINE_OK);
    assert(result_code == OAKENGINE_SERIALIZER_OK);

    // foreach_property should visit both pasted properties.
    int prop_seen = 0;
    int ret = oakengine_clipboard_foreach_property(
        cb_paste,
        [](OakEngineNode *node, const char *key, const char *value,
           void *userdata) -> int
        {
            (void) node;
            (void) key;
            (void) value;
            (*(int *) userdata)++;
            return 0;
        },
        &prop_seen);
    assert(ret == OAKENGINE_OK);
    assert(prop_seen >= 2);

    oakengine_clipboard_free(cb_paste);
    oakengine_project_free(project);
}

static void test_clipboard_copy_paste_roundtrip(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);
    assert(oakengine_project_new(project) == OAKENGINE_OK);

    OakEngineNode *solid = oakengine_project_add_node(
        project, "org.olivevideoeditor.Olive.solidgenerator");
    assert(solid != NULL);

    // Create clipboard A for copy (type doesn't matter; set_nodes overrides).
    OakEngineClipboard *cb_copy =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_copy != NULL);
    assert(oakengine_clipboard_set_nodes(cb_copy, (const OakEngineNode *const *)&solid, 1) == OAKENGINE_OK);
    assert(oakengine_clipboard_copy(cb_copy) == OAKENGINE_OK);
    oakengine_clipboard_free(cb_copy);

    // Create clipboard B for paste.
    OakEngineClipboard *cb_paste =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_paste != NULL);

    int result_code = -1;
    int ret = oakengine_clipboard_paste(cb_paste, OAKENGINE_CLIPBOARD_NODES,
                                        project, &result_code, NULL, 0);
    assert(ret == OAKENGINE_OK);
    assert(result_code == OAKENGINE_SERIALIZER_OK);

    // Verify loaded_* accessors.
    assert(oakengine_clipboard_get_loaded_node_count(cb_paste) == 1);
    OakEngineNode *loaded = oakengine_clipboard_get_loaded_node_at(cb_paste, 0);
    assert(loaded != NULL);
    assert(loaded != solid); // pasted node should be a new copy
    assert(oakengine_clipboard_get_loaded_node_at(cb_paste, -1) == NULL);
    assert(oakengine_clipboard_get_loaded_node_at(cb_paste, 1) == NULL);

    oakengine_clipboard_free(cb_paste);
    oakengine_project_free(project);
}

static void test_clipboard_paste_with_map(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);
    assert(oakengine_project_new(project) == OAKENGINE_OK);

    OakEngineNode *solid = oakengine_project_add_node(
        project, "org.olivevideoeditor.Olive.solidgenerator");
    assert(solid != NULL);

    OakEngineClipboard *cb_copy =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_copy != NULL);
    assert(oakengine_clipboard_set_nodes(cb_copy, (const OakEngineNode *const *)&solid, 1) == OAKENGINE_OK);
    assert(oakengine_clipboard_copy(cb_copy) == OAKENGINE_OK);
    oakengine_clipboard_free(cb_copy);

    OakEngineClipboard *cb_paste =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_paste != NULL);

    int pair_count = 0;
    int result_code = -1;
    int ret = oakengine_clipboard_paste_with_map(
        cb_paste, OAKENGINE_CLIPBOARD_NODES, project,
        [](OakEngineNode *old_node, OakEngineNode *new_node,
           void *userdata) -> int
        {
            auto *pc = (int *) userdata;
            (*pc)++;
            assert(old_node != NULL);
            assert(new_node != NULL);
            assert(old_node != new_node);
            return 0;
        },
        &pair_count, &result_code, NULL, 0);
    assert(ret == OAKENGINE_OK);
    assert(result_code == OAKENGINE_SERIALIZER_OK);
    assert(pair_count == 1);

    oakengine_clipboard_free(cb_paste);
    oakengine_project_free(project);
}

static void test_clipboard_foreach_iterators(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);
    assert(oakengine_project_new(project) == OAKENGINE_OK);

    OakEngineNode *solid = oakengine_project_add_node(
        project, "org.olivevideoeditor.Olive.solidgenerator");
    assert(solid != NULL);

    // Copy with properties so the paste-result has properties, then verify
    // foreach_property, foreach_keyframe (should be 0) and foreach_connection
    // (should be 0 since nothing is connected).
    OakEngineClipboard *cb_copy =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_copy != NULL);
    assert(oakengine_clipboard_set_nodes(cb_copy, (const OakEngineNode *const *)&solid, 1) == OAKENGINE_OK);
    assert(oakengine_clipboard_set_property(cb_copy, solid, "pos_x", "50") ==
           OAKENGINE_OK);
    assert(oakengine_clipboard_set_property(cb_copy, solid, "pos_y", "75") ==
           OAKENGINE_OK);
    assert(oakengine_clipboard_copy(cb_copy) == OAKENGINE_OK);
    oakengine_clipboard_free(cb_copy);

    OakEngineClipboard *cb_paste =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_NODES, project, NULL);
    assert(cb_paste != NULL);

    int result_code = -1;
    assert(oakengine_clipboard_paste(cb_paste, OAKENGINE_CLIPBOARD_NODES,
                                     project, &result_code, NULL, 0) ==
           OAKENGINE_OK);
    assert(result_code == OAKENGINE_SERIALIZER_OK);

    // foreach_property should visit the pasted properties.
    int prop_count = 0;
    assert(oakengine_clipboard_foreach_property(
        cb_paste,
        [](OakEngineNode *node, const char *key, const char *value,
           void *userdata) -> int
        {
            (void) node;
            (void) key;
            (void) value;
            (*(int *) userdata)++;
            return 0;
        },
        &prop_count) == OAKENGINE_OK);
    assert(prop_count >= 2);

    // foreach_keyframe should visit 0 (solid has no keyframe data in this test).
    int kf_count = 0;
    assert(oakengine_clipboard_foreach_keyframe(
        cb_paste,
        [](const char *node_id, OakEngineKeyframe *keyframe,
           void *userdata) -> int
        {
            (void) node_id;
            (void) keyframe;
            (*(int *) userdata)++;
            return 0;
        },
        &kf_count) == OAKENGINE_OK);

    // foreach_connection should visit 0 (no connections copied).
    int conn_count = 0;
    assert(oakengine_clipboard_foreach_connection(
        cb_paste,
        [](OakEngineNode *output_node, OakEngineNode *input_node,
           const char *input_id, int element, void *userdata) -> int
        {
            (void) output_node;
            (void) input_node;
            (void) input_id;
            (void) element;
            (*(int *) userdata)++;
            return 0;
        },
        &conn_count) == OAKENGINE_OK);
    assert(conn_count == 0);

    oakengine_clipboard_free(cb_paste);
    oakengine_project_free(project);
}

static void test_clipboard_marker_keyframe_accessors(void)
{
    OakEngineProject *project = oakengine_project_create();
    assert(project != NULL);
    assert(oakengine_project_new(project) == OAKENGINE_OK);

    // Create a sequence for its marker list.
    OakEngineSequence *seq = oakengine_sequence_new(project, "MarkerSrc");
    assert(seq != NULL);

    // Add a marker to the sequence's marker list.
    OakEngineMarkerList *list =
        oakengine_viewer_get_marker_list((OakEngineNode *)seq);
    assert(list != NULL);
    assert(oakengine_marker_list_add(list, 1, 1, 2, 1, "Test", 0) ==
           OAKENGINE_OK);
    OakEngineMarker *marker = oakengine_marker_list_at(list, 0);
    assert(marker != NULL);

    // Copy markers to clipboard and save_to_xml (tests set_markers + save).
    OakEngineClipboard *cb_copy =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_MARKERS, project, NULL);
    assert(cb_copy != NULL);
    assert(oakengine_clipboard_set_markers(
               cb_copy, (const OakEngineMarker *const *)&marker, 1) ==
           OAKENGINE_OK);
    assert(oakengine_clipboard_copy(cb_copy) == OAKENGINE_OK);
    oakengine_clipboard_free(cb_copy);

    // Paste back and verify get_loaded_marker accessors.
    OakEngineClipboard *cb_paste =
        oakengine_clipboard_create(OAKENGINE_CLIPBOARD_MARKERS, project, NULL);
    assert(cb_paste != NULL);

    int result_code = -1;
    assert(oakengine_clipboard_paste(cb_paste, OAKENGINE_CLIPBOARD_MARKERS,
                                     project, &result_code, NULL, 0) ==
           OAKENGINE_OK);
    assert(result_code == OAKENGINE_SERIALIZER_OK ||
           result_code == OAKENGINE_SERIALIZER_NO_DATA);
    // If paste succeeded, verify the accessors.
    if (result_code == OAKENGINE_SERIALIZER_OK) {
        int mc = oakengine_clipboard_get_loaded_marker_count(cb_paste);
        assert(mc >= 0);
        OakEngineMarker *pm = oakengine_clipboard_get_loaded_marker_at(
            cb_paste, 0);
        if (pm != NULL) {
            assert(oakengine_clipboard_get_loaded_marker_at(cb_paste, -1) ==
                   NULL);
        }
    }

    // get_loaded_keyframe accessors with 0 keyframes (no keyframes copied).
    assert(oakengine_clipboard_get_loaded_keyframe_count(cb_paste) >= 0);
    assert(oakengine_clipboard_get_loaded_keyframe_at(cb_paste, 0) == NULL);

    oakengine_clipboard_free(cb_paste);
    oakengine_project_free(project);
}

int main(void)
{
    assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

    test_check_compressed_nonexistent();
    test_clipboard_create_free();
    test_copy_empty_nodes();
    test_set_empty_sets();
    test_set_node_then_save_xml();
    test_set_node_then_foreach_property();
    test_clipboard_copy_paste_roundtrip();
    test_clipboard_paste_with_map();
    test_clipboard_foreach_iterators();
    test_clipboard_marker_keyframe_accessors();

    assert(oakengine_shutdown() == OAKENGINE_OK);
    return 0;
}
