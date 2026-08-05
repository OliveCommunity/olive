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

#ifndef OAK_IPC_IPCMESSAGE_H
#define OAK_IPC_IPCMESSAGE_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "oakengine/ipc.h"

namespace olive
{
namespace ipc
{

/**
 * @brief Control-plane protocol exchanged over stdio between main and render worker.
 *
 * The wire format is NDJSON: one compact JSON object per line, terminated by '\n'. This is
 * deliberately human-readable so the channel can be inspected live with `tee`/`cat` and test
 * messages can be injected by hand. The stdio channel carries only low-frequency control traffic;
 * bulk pixel data travels through the shared-memory FrameSlotPool, and the (potentially large)
 * serialized node graph travels via a temporary file referenced by path.
 *
 * Consumer-side wrapper over the liboakengine C ABI: the typed builders/parsers below convert
 * through the oakengine_ipc_*_to_json/parse functions, so the JSON field names and the wire
 * format are defined exactly once, inside the library, and stay in lockstep with the worker.
 * A message "object" on this side is simply the compact JSON text (`JsonMessage`); no Qt JSON
 * types are involved anymore.
 *
 * Every message object has a "type" string field. Directionality (M = main, W = worker):
 *   "handshake"     M<->W  Negotiate protocol version and announce shared-memory key/geometry.
 *   "load_graph"    M ->W  Path to a temporary file holding the serialized node graph.
 *   "render_frame"  M ->W  Request a frame: node uuid, time, video params.
 *   "frame_ready"   W ->M  A rendered frame is published; carries the output-slot index + ticket.
 *   "cancel"        M ->W  Abandon an in-flight ticket by id.
 *   "graph_update"  M ->W  (Reserved, Phase 6) Incremental graph mutation, mirrors ProjectCopier.
 *   "shutdown"      M ->W  Finish current work and exit cleanly.
 *   "error"         W ->M  Worker-side failure report (human-readable "message" field).
 */

/**
 * @brief One control-plane message as compact JSON object text (no trailing newline).
 */
using JsonMessage = std::string;

namespace msgtype
{
constexpr const char *k_handshake = OAKENGINE_IPC_MSGTYPE_HANDSHAKE;
constexpr const char *k_load_graph = OAKENGINE_IPC_MSGTYPE_LOAD_GRAPH;
constexpr const char *k_render_frame = OAKENGINE_IPC_MSGTYPE_RENDER_FRAME;
constexpr const char *k_frame_ready = OAKENGINE_IPC_MSGTYPE_FRAME_READY;
constexpr const char *k_cancel = OAKENGINE_IPC_MSGTYPE_CANCEL;
constexpr const char *k_graph_update = OAKENGINE_IPC_MSGTYPE_GRAPH_UPDATE;
constexpr const char *k_shutdown = OAKENGINE_IPC_MSGTYPE_SHUTDOWN;
constexpr const char *k_error = OAKENGINE_IPC_MSGTYPE_ERROR;
} // namespace msgtype

namespace detail
{

inline void copy_str(const std::string &s, char *dst, size_t cap)
{
	const size_t n = std::min(s.size(), cap - 1);
	memcpy(dst, s.data(), n);
	dst[n] = '\0';
}

/**
 * @brief Run a C to_json function (buf/size convention) and return the compact JSON text.
 */
template <typename F> std::string via_c_json(F &&to_json)
{
	const int size = to_json(nullptr, 0);
	std::string buf(size + 1, '\0');
	to_json(buf.data(), size + 1);
	buf.resize(size);
	return buf;
}

} // namespace detail

/**
 * @brief Write one NDJSON message line to `device`.
 *
 * Appends '\n' to the compact JSON text and writes the whole line in one call. Returns true only
 * if the full line was written. `Device` is anything with a
 * `write(const char *, int64_t)`-shaped method (QProcess during the transition, a plain pipe
 * wrapper later).
 */
template <typename Device>
bool write_message(Device *device, const JsonMessage &obj)
{
	std::string line = obj;
	line.push_back('\n');
	return device->write(line.data(), int64_t(line.size())) ==
		   int64_t(line.size());
}

/**
 * @brief Pull one complete NDJSON line out of `buffer`.
 *
 * If `buffer` contains at least one '\n', the leading line is removed, validated, and returned
 * via `out` (true). If no complete line is buffered yet, leaves `buffer` untouched and returns
 * false. Malformed lines are skipped (removed) and reported via `*ok = false` so the reader can
 * log and continue rather than wedge. Supports the typical "append bytes as they arrive, then
 * drain complete lines" reader loop on a pipe.
 *
 * Validation is delegated to oakengine_ipc_message_type(): a line counts as well-formed when it
 * is a JSON object carrying a recognized "type" field. (The Qt original accepted any syntactically
 * valid JSON object here; the per-type from_json() parsers still reject wrong-type lines, so the
 * only behavioral difference is that valid-JSON-but-unknown-type lines are now reported as
 * malformed at this layer.)
 */
inline bool read_message(std::string *buffer, JsonMessage *out,
						 bool *ok = nullptr)
{
	while (true) {
		const std::string::size_type newline = buffer->find('\n');
		if (newline == std::string::npos) {
			// No complete line buffered yet.
			return false;
		}

		std::string line = buffer->substr(0, newline);
		buffer->erase(0, newline + 1);

		// Skip blank lines silently (e.g. a stray newline) without flagging an error.
		const std::string::size_type first_non_space =
			line.find_first_not_of(" \t\r");
		if (first_non_space == std::string::npos) {
			continue;
		}

		if (oakengine_ipc_message_type(line.c_str()) ==
			OAK_IPC_MSGTYPE_UNKNOWN) {
			if (ok) {
				*ok = false;
			}
			return false;
		}

		*out = std::move(line);
		if (ok) {
			*ok = true;
		}
		return true;
	}
}

// ---- Typed message builders / parsers -------------------------------------------------------
//
// Thin wrappers that convert each struct to/from the C ABI POD form and let the library build or
// read the JSON, keeping field names in one place so main and worker agree. Fields use plain JSON
// numbers/strings; 64-bit ids are stored as JSON numbers (doubles exactly represent integers up
// to 2^53, ample for our counters).

struct HandshakeMsg {
	int protocol_version = 0;
	std::string shm_key; ///< Worker->main output shared-memory segment key.
	std::string
		input_shm_key; ///< Main->worker input shared-memory segment key (optional).
	int input_slots = 0; ///< Number of main->worker input frame slots.
	int output_slots = 0; ///< Number of worker->main output frame slots.
	int64_t slot_data_bytes = 0; ///< Per-output-slot pixel block size.
	int64_t input_slot_data_bytes = 0; ///< Per-input-slot pixel block size.

	JsonMessage to_json() const
	{
		oak_ipc_handshake c;
		c.protocol_version = protocol_version;
		detail::copy_str(shm_key, c.shm_key, sizeof(c.shm_key));
		detail::copy_str(input_shm_key, c.input_shm_key,
						 sizeof(c.input_shm_key));
		c.input_slots = input_slots;
		c.output_slots = output_slots;
		c.slot_data_bytes = slot_data_bytes;
		c.input_slot_data_bytes = input_slot_data_bytes;
		return detail::via_c_json([&](char *buf, int size) {
			return oakengine_ipc_handshake_to_json(&c, buf, size);
		});
	}

	static bool from_json(const JsonMessage &o, HandshakeMsg *out)
	{
		oak_ipc_handshake c;
		if (!oakengine_ipc_handshake_parse(o.c_str(), &c)) {
			return false;
		}
		out->protocol_version = c.protocol_version;
		out->shm_key = c.shm_key;
		out->input_shm_key = c.input_shm_key;
		out->input_slots = c.input_slots;
		out->output_slots = c.output_slots;
		out->slot_data_bytes = c.slot_data_bytes;
		out->input_slot_data_bytes = c.input_slot_data_bytes;
		return true;
	}
};

struct RenderFrameMsg {
	int64_t ticket_id =
		0; ///< Correlates this request with the eventual frame_ready.
	std::string
		node_uuid; ///< Output/viewer node to render, by stable uuid in the loaded graph.
	int64_t time_num = 0;
	int64_t time_den = 1;
	int width = 0; ///< Forced output size (0 = use graph default).
	int height = 0;
	int format = -1; ///< Forced PixelFormat::Format (-1 = default/INVALID).
	int channel_count = 0; ///< 0 = default.
	int mode = 0; ///< RenderMode::Mode.
	int input_slot =
		-1; ///< Optional main->worker decoded input slot for footage nodes.
	std::vector<int>
		input_slots; ///< Optional ordered decoded input slots for footage nodes.

	// Output color transform to apply before returning the frame. When empty,
	// the worker returns the image in the project's reference space.
	bool has_color_transform = false;
	bool color_is_display = false;
	std::string color_output;
	std::string color_view;
	std::string color_look;

	JsonMessage to_json() const
	{
		oak_ipc_render_frame c;
		c.ticket_id = ticket_id;
		detail::copy_str(node_uuid, c.node_uuid, sizeof(c.node_uuid));
		c.time_num = time_num;
		c.time_den = time_den;
		c.width = width;
		c.height = height;
		c.format = format;
		c.channel_count = channel_count;
		c.mode = mode;
		c.input_slot = input_slot;
		c.input_slot_count = std::min(int(input_slots.size()),
									  OAK_IPC_INPUT_SLOTS_CAP);
		for (int i = 0; i < c.input_slot_count; i++) {
			c.input_slots[i] = input_slots.at(i);
		}
		c.has_color_transform = has_color_transform ? 1 : 0;
		c.color_is_display = color_is_display ? 1 : 0;
		detail::copy_str(color_output, c.color_output,
						 sizeof(c.color_output));
		detail::copy_str(color_view, c.color_view, sizeof(c.color_view));
		detail::copy_str(color_look, c.color_look, sizeof(c.color_look));
		return detail::via_c_json([&](char *buf, int size) {
			return oakengine_ipc_render_frame_to_json(&c, buf, size);
		});
	}

	static bool from_json(const JsonMessage &o, RenderFrameMsg *out)
	{
		oak_ipc_render_frame c;
		if (!oakengine_ipc_render_frame_parse(o.c_str(), &c)) {
			return false;
		}
		out->ticket_id = c.ticket_id;
		out->node_uuid = c.node_uuid;
		out->time_num = c.time_num;
		out->time_den = c.time_den;
		out->width = c.width;
		out->height = c.height;
		out->format = c.format;
		out->channel_count = c.channel_count;
		out->mode = c.mode;
		out->input_slot = c.input_slot;
		out->input_slots.clear();
		for (int i = 0; i < c.input_slot_count; i++) {
			out->input_slots.push_back(c.input_slots[i]);
		}
		out->has_color_transform = c.has_color_transform != 0;
		out->color_is_display = c.color_is_display != 0;
		out->color_output = c.color_output;
		out->color_view = c.color_view;
		out->color_look = c.color_look;
		return true;
	}
};

struct FrameReadyMsg {
	int64_t ticket_id = 0;
	int output_slot = 0; ///< Index into the worker->main output FrameSlotPool.

	JsonMessage to_json() const
	{
		oak_ipc_frame_ready c;
		c.ticket_id = ticket_id;
		c.output_slot = output_slot;
		return detail::via_c_json([&](char *buf, int size) {
			return oakengine_ipc_frame_ready_to_json(&c, buf, size);
		});
	}

	static bool from_json(const JsonMessage &o, FrameReadyMsg *out)
	{
		oak_ipc_frame_ready c;
		if (!oakengine_ipc_frame_ready_parse(o.c_str(), &c)) {
			return false;
		}
		out->ticket_id = c.ticket_id;
		out->output_slot = c.output_slot;
		return true;
	}
};

struct CancelMsg {
	int64_t ticket_id = 0;

	JsonMessage to_json() const
	{
		oak_ipc_cancel c;
		c.ticket_id = ticket_id;
		return detail::via_c_json([&](char *buf, int size) {
			return oakengine_ipc_cancel_to_json(&c, buf, size);
		});
	}

	static bool from_json(const JsonMessage &o, CancelMsg *out)
	{
		oak_ipc_cancel c;
		if (!oakengine_ipc_cancel_parse(o.c_str(), &c)) {
			return false;
		}
		out->ticket_id = c.ticket_id;
		return true;
	}
};

struct LoadGraphMsg {
	std::string path; ///< Temporary file holding the serialized node graph.

	JsonMessage to_json() const
	{
		oak_ipc_load_graph c;
		detail::copy_str(path, c.path, sizeof(c.path));
		return detail::via_c_json([&](char *buf, int size) {
			return oakengine_ipc_load_graph_to_json(&c, buf, size);
		});
	}

	static bool from_json(const JsonMessage &o, LoadGraphMsg *out)
	{
		oak_ipc_load_graph c;
		if (!oakengine_ipc_load_graph_parse(o.c_str(), &c)) {
			return false;
		}
		out->path = c.path;
		return true;
	}
};

} // namespace ipc
} // namespace olive

#endif // OAK_IPC_IPCMESSAGE_H
