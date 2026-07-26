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

#ifndef OAKENGINE_WORKER_H
#define OAKENGINE_WORKER_H

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file worker.h
 * @brief C ABI for the render worker process logic
 *
 * The render worker (oak-render-worker) is a headless render process spawned
 * by the editor through the render worker pool. All of its runtime logic —
 * Qt application setup, render backend initialization, the startup handshake
 * and the NDJSON control-message loop — lives inside liboakengine behind this
 * pure C interface, so the worker executable itself contains no engine C++
 * ABI usage.
 *
 * Two entry levels are exposed:
 *
 *   - oakengine_worker_main(): a drop-in main() for the worker executable.
 *     It creates the QGuiApplication, parses --backend, initializes the
 *     renderer, sends the startup handshake and runs the stdin/stdout NDJSON
 *     loop until a shutdown message or EOF.
 *
 *   - The OakWorkerSession family: the same message-handling state machine
 *     in a transport-agnostic form, so tests (and alternative transports)
 *     can drive it line by line without spawning a process. Responses that
 *     the worker would write to stdout are returned through the buf/size
 *     convention instead.
 *
 * Conventions (mirrors ipc.h):
 *   - Returned handles are owned by the caller and must be released with the
 *     matching _free(). NULL is accepted by every function and yields a
 *     no-op / zero result.
 *   - String output uses the buf/size convention: the return value is the
 *     number of characters that would have been written excluding the NUL,
 *     so buf == NULL or a short buffer queries the required size. The output
 *     is NUL-terminated whenever buf_size > 0.
 */

typedef struct OakWorkerSession OakWorkerSession;

/**
 * @brief Create a worker session for the given render backend.
 *
 * `backend` names the render backend ("opengl", "vulkan"); the session tries
 * the dynamic backend first and falls back to the direct OpenGL renderer,
 * exactly like the worker main. NULL, "" or "none" skips renderer creation
 * entirely, producing a session that can parse and answer control messages
 * but cannot actually render (useful for exercising error paths in tests).
 *
 * A QGuiApplication must exist before creating a session with a real
 * backend. The returned handle is owned by the caller.
 */
OAKENGINE_API OakWorkerSession *
oakengine_worker_session_create(const char *backend);

OAKENGINE_API void oakengine_worker_session_free(OakWorkerSession *self);

/**
 * @brief 1 if the session holds a successfully initialized render backend.
 */
OAKENGINE_API int
oakengine_worker_session_has_renderer(const OakWorkerSession *self);

/**
 * @brief Load the engine runtime services the session depends on (config,
 * node factory, color manager, frame/disk managers, project serializer).
 *
 * Idempotent in practice: the underlying services are process-wide
 * singletons. Returns 1 on success, 0 on failure (NULL session).
 */
OAKENGINE_API int
oakengine_worker_session_initialize_runtime(OakWorkerSession *self);

/**
 * @brief Build the startup handshake the worker sends to its parent
 * (buf/size convention).
 *
 * Announces the protocol version and, when a renderer is present, the
 * negotiated GL version. Returns the required size, or -1 on failure.
 */
OAKENGINE_API int
oakengine_worker_session_startup_handshake(OakWorkerSession *self, char *buf,
										   int buf_size);

/**
 * @brief Handle one NDJSON control line and produce the response, if any.
 *
 * `line` is one complete JSON message (with or without the trailing
 * newline). The response — what the worker main loop would write to stdout —
 * is serialized into response_buf using the buf/size convention: the return
 * value is the number of characters that would have been written excluding
 * the NUL, so 0 means "no response" (e.g. a successful handshake or a
 * shutdown message) and a positive value queries/fills the response. A
 * malformed `line` yields an error response, not a failure.
 *
 * Returns -1 when the handler itself failed (the worker main treats this as
 * a fatal error for its exit code, though it keeps draining input).
 */
OAKENGINE_API int
oakengine_worker_session_handle_json(OakWorkerSession *self, const char *line,
									 char *response_buf, int response_buf_size);

/**
 * @brief 1 once a shutdown control message has been received.
 */
OAKENGINE_API int
oakengine_worker_session_shutdown_requested(const OakWorkerSession *self);

/**
 * @brief Full render-worker main(). `argc`/`argv` are passed through from
 * the executable's main; "--backend <name>" selects the render backend.
 *
 * Returns the process exit code (0 on clean shutdown).
 */
OAKENGINE_API int oakengine_worker_main(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_WORKER_H */
