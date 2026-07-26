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

// Pure C ABI test for the liboakengine render-worker facade
// (oakengine/worker.h). Exercises the session state machine without a
// renderer ("none" backend): create/destroy, malformed and unknown control
// messages, handshake validation, message ordering errors and shutdown
// idempotency. No GPU and no QApplication required: every path exercised
// here is a validation/error path that never touches a render backend.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oakengine/worker.h"

// Handle one line into a heap buffer sized via the buf/size query
// convention. Returns the response (empty string when the message has no
// reply); the caller frees it. Asserts the query/fill round-trip agrees.
static char *handle(OakWorkerSession *session, const char *line)
{
	const int needed =
		oakengine_worker_session_handle_json(session, line, NULL, 0);
	assert(needed >= 0);
	char *buf = static_cast<char *>(malloc(size_t(needed) + 1));
	const int written = oakengine_worker_session_handle_json(
		session, line, buf, needed + 1);
	assert(written == needed);
	buf[needed] = '\0';
	return buf;
}

static void assert_is_error_with(const char *response, const char *needle)
{
	if (!strstr(response, "\"type\":\"error\"") ||
		!strstr(response, needle)) {
		fprintf(stderr,
				"expected error response containing \"%s\", got: %s\n",
				needle, response);
		assert(0);
	}
}

static void test_create_destroy(void)
{
	// NULL, "" and "none" all skip renderer creation
	const char *backends[] = { NULL, "", "none", "NONE" };
	for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); ++i) {
		OakWorkerSession *s = oakengine_worker_session_create(backends[i]);
		assert(s);
		assert(oakengine_worker_session_has_renderer(s) == 0);
		assert(oakengine_worker_session_shutdown_requested(s) == 0);
		oakengine_worker_session_free(s);
	}
	// NULL tolerance
	oakengine_worker_session_free(NULL);
	assert(oakengine_worker_session_has_renderer(NULL) == 0);
	assert(oakengine_worker_session_shutdown_requested(NULL) == 0);
	assert(oakengine_worker_session_handle_json(NULL, "{}", NULL, 0) == -1);
}

static void test_startup_handshake(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");
	assert(s);

	// buf/size query convention
	const int needed =
		oakengine_worker_session_startup_handshake(s, NULL, 0);
	assert(needed > 0);
	char *buf = static_cast<char *>(malloc(size_t(needed) + 1));
	assert(oakengine_worker_session_startup_handshake(s, buf, needed + 1) ==
		   needed);
	buf[needed] = '\0';
	assert(strstr(buf, "\"type\":\"handshake\""));
	assert(strstr(buf, "\"protocol_version\":1"));
	// No renderer -> no GL version announced
	assert(!strstr(buf, "gl_major"));
	free(buf);

	assert(oakengine_worker_session_startup_handshake(NULL, NULL, 0) == -1);
	oakengine_worker_session_free(s);
}

static void test_initialize_runtime(void)
{
	// NULL tolerance
	assert(oakengine_worker_session_initialize_runtime(NULL) == 0);

	// Runtime init (EngineCore, factories, managers) must succeed without a
	// renderer; the session stays usable for control messages afterwards.
	OakWorkerSession *s = oakengine_worker_session_create("none");
	assert(s);
	assert(oakengine_worker_session_initialize_runtime(s) == 1);
	char *r = handle(s, "{\"type\":\"teleport\"}");
	assert_is_error_with(r, "unknown message type");
	free(r);
	oakengine_worker_session_free(s);
}

static void test_malformed_json(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");
	char *r = handle(s, "{not json at all");
	assert_is_error_with(r, "malformed control message");
	free(r);
	r = handle(s, "[1,2,3]");
	assert_is_error_with(r, "malformed control message");
	free(r);
	oakengine_worker_session_free(s);
}

static void test_unknown_type(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");
	char *r = handle(s, "{\"type\":\"teleport\"}");
	assert_is_error_with(r, "unknown message type: teleport");
	free(r);
	oakengine_worker_session_free(s);
}

static void test_handshake_validation(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");

	// Protocol version mismatch is rejected before any shm access
	char *r = handle(s,
					 "{\"type\":\"handshake\",\"protocol_version\":999,"
					 "\"shm_key\":\"x\",\"output_slots\":1,"
					 "\"slot_data_bytes\":16}");
	assert_is_error_with(r, "unsupported protocol version 999");
	free(r);

	// Matching version but no shared-memory geometry
	r = handle(s, "{\"type\":\"handshake\",\"protocol_version\":1}");
	assert_is_error_with(r, "missing output shared-memory geometry");
	free(r);

	oakengine_worker_session_free(s);
}

static void test_render_frame_before_load_graph(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");
	char *r = handle(s,
					 "{\"type\":\"render_frame\",\"ticket\":7,"
					 "\"node_uuid\":\"abc\",\"time_num\":0,\"time_den\":1}");
	assert_is_error_with(r, "render_frame received before load_graph");
	// The error carries the ticket id so the caller can correlate
	assert(strstr(r, "\"ticket\":7"));
	free(r);
	oakengine_worker_session_free(s);
}

static void test_load_graph_missing_file(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");
	char *r = handle(s,
					 "{\"type\":\"load_graph\","
					 "\"path\":\"/nonexistent/definitely/missing.ove\"}");
	assert_is_error_with(r, "graph file does not exist");
	free(r);
	// A failed load must not arm the session: render_frame still complains
	// about the missing graph, not about the shm handshake order
	r = handle(s,
			   "{\"type\":\"render_frame\",\"ticket\":1,"
			   "\"node_uuid\":\"abc\",\"time_num\":0,\"time_den\":1}");
	assert_is_error_with(r, "render_frame received before load_graph");
	free(r);
	oakengine_worker_session_free(s);
}

static void test_shutdown_idempotent(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");

	// Shutdown produces no response and latches the flag
	char *r = handle(s, "{\"type\":\"shutdown\"}");
	assert(r[0] == '\0');
	free(r);
	assert(oakengine_worker_session_shutdown_requested(s) == 1);

	// Repeating it is a harmless no-op
	r = handle(s, "{\"type\":\"shutdown\"}");
	assert(r[0] == '\0');
	free(r);
	assert(oakengine_worker_session_shutdown_requested(s) == 1);

	// The session still answers other messages afterwards
	r = handle(s, "{\"type\":\"teleport\"}");
	assert_is_error_with(r, "unknown message type");
	free(r);

	oakengine_worker_session_free(s);
}

static void test_cancel_is_silent(void)
{
	OakWorkerSession *s = oakengine_worker_session_create("none");
	char *r = handle(s, "{\"type\":\"cancel\",\"ticket\":3}");
	assert(r[0] == '\0');
	free(r);
	oakengine_worker_session_free(s);
}

int main(void)
{
	test_create_destroy();
	test_startup_handshake();
	test_initialize_runtime();
	test_malformed_json();
	test_unknown_type();
	test_handshake_validation();
	test_render_frame_before_load_graph();
	test_load_graph_missing_file();
	test_shutdown_idempotent();
	test_cancel_is_silent();
	return 0;
}
