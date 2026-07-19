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

#ifndef OAKENGINE_IPC_H
#define OAKENGINE_IPC_H

#include <stddef.h>
#include <stdint.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file ipc.h
 * @brief C ABI for the render worker IPC subsystem
 *
 * Two channels are covered:
 *
 *   - Bulk data: named shared-memory segments (OakSharedMemoryRegion) holding
 *     a fixed-size pool of frame slots (OakFrameSlotPool). The in-memory
 *     layout produced by oakengine_ipc_framepool_create() is the wire
 *     protocol (version 1) shared with the render worker binary and never
 *     changes. oak_frame_slot_meta is the POD metadata record that lives in
 *     that shared layout, so it is exposed here as a plain C struct.
 *
 *   - Control plane: newline-delimited JSON messages exchanged over stdio
 *     pipes. No Qt types cross this boundary; each message type has a POD
 *     struct plus build/parse functions converting between the struct and a
 *     compact JSON string.
 *
 * Conventions:
 *   - Returned Oak* handles are owned by the caller and must be released
 *     with the matching _free(). NULL is accepted by every function and
 *     yields a no-op / zero result.
 *   - Booleans are int (1/0). Fallible parses return 1 on success, 0 on
 *     failure.
 *   - String output uses the buf/size convention: the return value is the
 *     number of characters that would have been written excluding the NUL,
 *     so buf == NULL or a short buffer queries the required size. The
 *     output is NUL-terminated whenever buf_size > 0.
 *   - POD message structs carry strings in fixed-capacity inline buffers so
 *     they stay trivially copyable; overlong input is truncated at the
 *     capacity.
 */

/** @brief Capacity of oak_frame_slot_meta::colorspace, including the NUL. */
#define OAK_IPC_COLORSPACE_CAP 128
/** @brief Capacity of shared-memory key string fields, including the NUL. */
#define OAK_IPC_SHM_KEY_CAP 128
/** @brief Capacity of oak_ipc_render_frame::node_uuid, including the NUL. */
#define OAK_IPC_NODE_UUID_CAP 64
/** @brief Capacity of the color transform name fields, including the NUL. */
#define OAK_IPC_COLOR_STR_CAP 128
/** @brief Capacity of oak_ipc_load_graph::path, including the NUL. */
#define OAK_IPC_PATH_CAP 1024
/** @brief Maximum number of decoded input slots carried by one render_frame. */
#define OAK_IPC_INPUT_SLOTS_CAP 64
/** @brief Capacity of the message buffer filled by oakengine_ipc_error_parse. */
#define OAK_IPC_ERROR_MESSAGE_CAP 512

/**
 * @brief Message type strings on the control-plane wire format.
 *
 * Every control message is a compact JSON object on one line whose "type"
 * field carries one of these values. graph_update is reserved (no payload
 * struct is defined yet).
 */
#define OAKENGINE_IPC_MSGTYPE_HANDSHAKE "handshake"
#define OAKENGINE_IPC_MSGTYPE_LOAD_GRAPH "load_graph"
#define OAKENGINE_IPC_MSGTYPE_RENDER_FRAME "render_frame"
#define OAKENGINE_IPC_MSGTYPE_FRAME_READY "frame_ready"
#define OAKENGINE_IPC_MSGTYPE_CANCEL "cancel"
#define OAKENGINE_IPC_MSGTYPE_GRAPH_UPDATE "graph_update"
#define OAKENGINE_IPC_MSGTYPE_SHUTDOWN "shutdown"
#define OAKENGINE_IPC_MSGTYPE_ERROR "error"

/**
 * @brief Message type discriminator mirroring the msgtype strings.
 *
 * Same-named values for each wire message type; returned by
 * oakengine_ipc_message_type() so a C consumer can dispatch an incoming JSON
 * line without hard-coding the strings.
 */
typedef enum oak_ipc_msgtype {
	OAK_IPC_MSGTYPE_UNKNOWN = -1,
	OAK_IPC_MSGTYPE_HANDSHAKE = 0,
	OAK_IPC_MSGTYPE_LOAD_GRAPH,
	OAK_IPC_MSGTYPE_RENDER_FRAME,
	OAK_IPC_MSGTYPE_FRAME_READY,
	OAK_IPC_MSGTYPE_CANCEL,
	OAK_IPC_MSGTYPE_GRAPH_UPDATE,
	OAK_IPC_MSGTYPE_SHUTDOWN,
	OAK_IPC_MSGTYPE_ERROR
} oak_ipc_msgtype;

/**
 * @brief Open mode for oakengine_ipc_shm_open().
 */
typedef enum oak_ipc_shm_mode {
	/** Create (and own) the segment. Fails if it exists; unlinks on close. */
	OAK_IPC_SHM_MODE_CREATE = 0,
	/** Attach to a segment created by the peer. Does not unlink on close. */
	OAK_IPC_SHM_MODE_ATTACH = 1
} oak_ipc_shm_mode;

/**
 * @brief Per-slot metadata describing the frame currently occupying a slot.
 *
 * Trivially-copyable POD that lives in shared memory alongside the pixel
 * data, part of the version-1 wire protocol between the app and the render
 * worker. Carries everything the consumer needs to reconstruct a frame
 * without any out-of-band information. The timestamp is stored as an
 * explicit numerator/denominator pair to stay POD.
 */
typedef struct oak_frame_slot_meta {
	int64_t id; /**< Caller-defined tag (e.g. ticket id, or footage stream hash). */
	int64_t time_num; /**< Frame timestamp numerator. */
	int64_t time_den; /**< Frame timestamp denominator. */
	int32_t width;
	int32_t height;
	int32_t format; /**< PixelFormat::Format value. */
	int32_t channel_count;
	int32_t linesize; /**< Bytes per scanline (stride). */
	int32_t data_size; /**< Valid bytes written into the slot's data block. */
	char colorspace[OAK_IPC_COLORSPACE_CAP]; /**< Input colorspace name. */
} oak_frame_slot_meta;

typedef struct OakSharedMemoryRegion OakSharedMemoryRegion;
typedef struct OakFrameSlotPool OakFrameSlotPool;

/* ---- SharedMemoryRegion ------------------------------------------------- */

/**
 * @brief Allocate an empty (invalid) region object. Owned by the caller.
 */
OAKENGINE_API OakSharedMemoryRegion *oakengine_ipc_shm_create(void);
OAKENGINE_API void oakengine_ipc_shm_free(OakSharedMemoryRegion *self);

/**
 * @brief Open the segment identified by `key` with the given `size` in bytes.
 *
 * `key` is a short identifier (no leading slash needed; the platform prefix
 * is added internally). Returns 1 on success; on failure returns 0 and
 * oakengine_ipc_shm_error() carries a human-readable reason.
 */
OAKENGINE_API int oakengine_ipc_shm_open(OakSharedMemoryRegion *self,
										 const char *key, size_t size,
										 oak_ipc_shm_mode mode);

/**
 * @brief Unmap and (if owner) unlink the segment. Also done by _free().
 */
OAKENGINE_API void oakengine_ipc_shm_close(OakSharedMemoryRegion *self);

OAKENGINE_API int oakengine_ipc_shm_is_valid(const OakSharedMemoryRegion *self);
OAKENGINE_API void *oakengine_ipc_shm_data(OakSharedMemoryRegion *self);
OAKENGINE_API size_t oakengine_ipc_shm_size(const OakSharedMemoryRegion *self);

/**
 * @brief The key the region was opened with (buf/size convention).
 */
OAKENGINE_API int oakengine_ipc_shm_key(const OakSharedMemoryRegion *self,
										char *buf, int buf_size);

/**
 * @brief Human-readable reason for the last failed open (buf/size convention).
 */
OAKENGINE_API int oakengine_ipc_shm_error(const OakSharedMemoryRegion *self,
										  char *buf, int buf_size);

/**
 * @brief Build a unique segment key for a worker, e.g. "olive-rw-<pid>-<index>".
 *
 * Centralized so the owner and the spawned worker agree on the same name.
 * Uses the buf/size convention.
 */
OAKENGINE_API int oakengine_ipc_shm_make_key(int64_t owner_pid,
											 int worker_index, char *buf,
											 int buf_size);

/* ---- FrameSlotPool ------------------------------------------------------ */

/**
 * @brief Total bytes a region must provide to back a pool of `slot_count` x
 * `slot_data_bytes`.
 */
OAKENGINE_API size_t
oakengine_ipc_framepool_bytes_needed(uint32_t slot_count,
									 size_t slot_data_bytes);

/**
 * @brief Lay out and initialize a brand-new pool over `mem` (owner side, once).
 *
 * `mem` must provide at least oakengine_ipc_framepool_bytes_needed() bytes
 * and must outlive the returned handle. The handle is owned by the caller;
 * it does not own `mem`.
 */
OAKENGINE_API OakFrameSlotPool *
oakengine_ipc_framepool_create(void *mem, uint32_t slot_count,
							   size_t slot_data_bytes);

/**
 * @brief Map an existing, already-initialized pool (peer side).
 *
 * Reads the geometry from the in-memory header written by _create(); the
 * handle reports is_valid() == 0 if the magic does not match.
 */
OAKENGINE_API OakFrameSlotPool *oakengine_ipc_framepool_attach(void *mem);

/**
 * @brief Copy the view (same shared memory, independent handle). Owned.
 */
OAKENGINE_API OakFrameSlotPool *
oakengine_ipc_framepool_copy(const OakFrameSlotPool *self);

OAKENGINE_API void oakengine_ipc_framepool_free(OakFrameSlotPool *self);

OAKENGINE_API int oakengine_ipc_framepool_is_valid(const OakFrameSlotPool *self);
OAKENGINE_API uint32_t
oakengine_ipc_framepool_slot_count(const OakFrameSlotPool *self);
OAKENGINE_API size_t
oakengine_ipc_framepool_slot_data_bytes(const OakFrameSlotPool *self);

/* Filler side: acquire a free slot, write meta + pixels, publish it. */

/**
 * @brief Take ownership of a free slot. Returns 0 (leaving *index untouched)
 * if none is free.
 */
OAKENGINE_API int oakengine_ipc_framepool_acquire(OakFrameSlotPool *self,
												  uint32_t *index);

/**
 * @brief Pointer to a slot's pixel data block (slot_data_bytes available).
 */
OAKENGINE_API void *oakengine_ipc_framepool_slot_data(OakFrameSlotPool *self,
													  uint32_t index);
OAKENGINE_API const void *
oakengine_ipc_framepool_slot_data_const(const OakFrameSlotPool *self,
										uint32_t index);

/**
 * @brief Mutable metadata for a slot. The filler writes this before publish.
 * The returned pointer addresses shared memory; it is borrowed, not owned.
 */
OAKENGINE_API oak_frame_slot_meta *
oakengine_ipc_framepool_meta(OakFrameSlotPool *self, uint32_t index);
OAKENGINE_API const oak_frame_slot_meta *
oakengine_ipc_framepool_meta_const(const OakFrameSlotPool *self,
								   uint32_t index);

/**
 * @brief Publish a filled slot to the drainer. Must follow a successful
 * acquire of `index`.
 */
OAKENGINE_API int oakengine_ipc_framepool_publish(OakFrameSlotPool *self,
												  uint32_t index);

/* Drainer side: consume the next published slot, read it, release it. */

/**
 * @brief Take the next published slot. Returns 0 if nothing is ready.
 */
OAKENGINE_API int oakengine_ipc_framepool_consume(OakFrameSlotPool *self,
												  uint32_t *index);

/**
 * @brief Return a consumed slot to the free pool. Must follow a consume of
 * `index`.
 */
OAKENGINE_API int oakengine_ipc_framepool_release(OakFrameSlotPool *self,
												  uint32_t index);

/* ---- Control-plane messages --------------------------------------------- */

/**
 * @brief Negotiate protocol version and announce shared-memory key/geometry.
 *
 * Field-for-field equivalent of the C++ HandshakeMsg; strings are inline
 * buffers with the documented capacities.
 */
typedef struct oak_ipc_handshake {
	int32_t protocol_version;
	char shm_key[OAK_IPC_SHM_KEY_CAP]; /**< Worker<-main output segment key. */
	char input_shm_key[OAK_IPC_SHM_KEY_CAP]; /**< Main->worker input key (optional). */
	int32_t input_slots; /**< Number of main->worker input frame slots. */
	int32_t output_slots; /**< Number of worker->main output frame slots. */
	int64_t slot_data_bytes; /**< Per-output-slot pixel block size. */
	int64_t input_slot_data_bytes; /**< Per-input-slot pixel block size. */
} oak_ipc_handshake;

/**
 * @brief Request a frame render: node uuid, time, video params.
 *
 * Field-for-field equivalent of the C++ RenderFrameMsg. `input_slots` holds
 * `input_slot_count` entries; the legacy scalar `input_slot` (-1 = none) is
 * kept in sync by the parse fallback exactly like the Qt implementation.
 */
typedef struct oak_ipc_render_frame {
	int64_t ticket_id; /**< Correlates with the eventual frame_ready. */
	char node_uuid[OAK_IPC_NODE_UUID_CAP]; /**< Viewer node stable uuid. */
	int64_t time_num;
	int64_t time_den;
	int32_t width; /**< Forced output size (0 = use graph default). */
	int32_t height;
	int32_t format; /**< Forced PixelFormat::Format (-1 = default/INVALID). */
	int32_t channel_count; /**< 0 = default. */
	int32_t mode; /**< RenderMode::Mode. */
	int32_t input_slot; /**< Optional decoded input slot (-1 = none). */
	int32_t input_slots[OAK_IPC_INPUT_SLOTS_CAP]; /**< Ordered decoded input slots. */
	int32_t input_slot_count; /**< Number of valid input_slots entries. */
	/* Output color transform; ignored unless has_color_transform != 0. */
	int32_t has_color_transform;
	int32_t color_is_display;
	char color_output[OAK_IPC_COLOR_STR_CAP];
	char color_view[OAK_IPC_COLOR_STR_CAP];
	char color_look[OAK_IPC_COLOR_STR_CAP];
} oak_ipc_render_frame;

/**
 * @brief A rendered frame is published; carries the output slot + ticket.
 */
typedef struct oak_ipc_frame_ready {
	int64_t ticket_id;
	int32_t output_slot; /**< Index into the worker->main output FrameSlotPool. */
} oak_ipc_frame_ready;

/**
 * @brief Abandon an in-flight ticket by id.
 */
typedef struct oak_ipc_cancel {
	int64_t ticket_id;
} oak_ipc_cancel;

/**
 * @brief Path to a temporary file holding the serialized node graph.
 */
typedef struct oak_ipc_load_graph {
	char path[OAK_IPC_PATH_CAP];
} oak_ipc_load_graph;

/**
 * @brief Identify the message type of one compact JSON line.
 *
 * Returns OAK_IPC_MSGTYPE_UNKNOWN for malformed JSON or an unrecognized
 * "type" field.
 */
OAKENGINE_API oak_ipc_msgtype oakengine_ipc_message_type(const char *json);

/**
 * @brief Serialize to compact JSON (buf/size convention). Returns -1 if
 * `self` is NULL.
 */
OAKENGINE_API int oakengine_ipc_handshake_to_json(
	const oak_ipc_handshake *self, char *buf, int buf_size);
/**
 * @brief Parse compact JSON. Returns 1 on success, 0 on type mismatch or
 * malformed input.
 */
OAKENGINE_API int oakengine_ipc_handshake_parse(const char *json,
												oak_ipc_handshake *out);

OAKENGINE_API int oakengine_ipc_render_frame_to_json(
	const oak_ipc_render_frame *self, char *buf, int buf_size);
OAKENGINE_API int oakengine_ipc_render_frame_parse(const char *json,
												   oak_ipc_render_frame *out);

OAKENGINE_API int oakengine_ipc_frame_ready_to_json(
	const oak_ipc_frame_ready *self, char *buf, int buf_size);
OAKENGINE_API int oakengine_ipc_frame_ready_parse(const char *json,
												  oak_ipc_frame_ready *out);

OAKENGINE_API int oakengine_ipc_cancel_to_json(const oak_ipc_cancel *self,
											   char *buf, int buf_size);
OAKENGINE_API int oakengine_ipc_cancel_parse(const char *json,
											 oak_ipc_cancel *out);

OAKENGINE_API int oakengine_ipc_load_graph_to_json(
	const oak_ipc_load_graph *self, char *buf, int buf_size);
OAKENGINE_API int oakengine_ipc_load_graph_parse(const char *json,
												 oak_ipc_load_graph *out);

/**
 * @brief Build the payload-less shutdown message (buf/size convention).
 */
OAKENGINE_API int oakengine_ipc_shutdown_to_json(char *buf, int buf_size);
/**
 * @brief Returns 1 if `json` is a shutdown message, 0 otherwise.
 */
OAKENGINE_API int oakengine_ipc_shutdown_parse(const char *json);

/**
 * @brief Build a worker-side error report with a human-readable message
 * (buf/size convention).
 */
OAKENGINE_API int oakengine_ipc_error_to_json(const char *message, char *buf,
											  int buf_size);
/**
 * @brief Parse an error message; the text is written into message_buf
 * (truncated at OAK_IPC_ERROR_MESSAGE_CAP-style buf size). Returns 1 on
 * success, 0 otherwise.
 */
OAKENGINE_API int oakengine_ipc_error_parse(const char *json,
											char *message_buf,
											int message_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_IPC_H */
