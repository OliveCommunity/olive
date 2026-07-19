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

#include "oakengine/ipc.h"

#include <cstdio>
#include <cstring>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include "oliveimpl/render/ipc/frameslotpool.h"
#include "oliveimpl/render/ipc/ipcmessage.h"
#include "oliveimpl/render/ipc/sharedmemoryregion.h"

namespace
{

namespace internal_ipc = olive::engine::internal::ipc;

olive::engine::internal::ipc::SharedMemoryRegion *impl(OakSharedMemoryRegion *h)
{
	return reinterpret_cast<olive::engine::internal::ipc::SharedMemoryRegion *>(
		h);
}

const olive::engine::internal::ipc::SharedMemoryRegion *
impl(const OakSharedMemoryRegion *h)
{
	return reinterpret_cast<
		const olive::engine::internal::ipc::SharedMemoryRegion *>(h);
}

OakSharedMemoryRegion *
wrap(olive::engine::internal::ipc::SharedMemoryRegion *r)
{
	return reinterpret_cast<OakSharedMemoryRegion *>(r);
}

olive::engine::internal::ipc::FrameSlotPool *impl(OakFrameSlotPool *h)
{
	return reinterpret_cast<olive::engine::internal::ipc::FrameSlotPool *>(h);
}

const olive::engine::internal::ipc::FrameSlotPool *
impl(const OakFrameSlotPool *h)
{
	return reinterpret_cast<const olive::engine::internal::ipc::FrameSlotPool *>(
		h);
}

OakFrameSlotPool *wrap(olive::engine::internal::ipc::FrameSlotPool *p)
{
	return reinterpret_cast<OakFrameSlotPool *>(p);
}

// Copy a QString into a fixed-capacity C buffer, always NUL-terminating and
// truncating what does not fit.
void copy_to_buf(const QString &s, char *dst, size_t cap)
{
	const QByteArray utf = s.toUtf8();
	const size_t n = qMin(size_t(utf.size()), cap - 1);
	memcpy(dst, utf.constData(), n);
	dst[n] = '\0';
}

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

int object_to_buf(const QJsonObject &o, char *buf, int buf_size)
{
	const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", json.constData());
	}
	return int(json.size());
}

bool parse_object(const char *json, QJsonObject *out)
{
	if (!json) {
		return false;
	}
	QJsonParseError err;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
	if (err.error != QJsonParseError::NoError || !doc.isObject()) {
		return false;
	}
	*out = doc.object();
	return true;
}

// ---- POD <-> impl message conversions ------------------------------------

void to_c(const internal_ipc::HandshakeMsg &in, oak_ipc_handshake *out)
{
	memset(out, 0, sizeof(*out));
	out->protocol_version = in.protocol_version;
	copy_to_buf(in.shm_key, out->shm_key, sizeof(out->shm_key));
	copy_to_buf(in.input_shm_key, out->input_shm_key,
				sizeof(out->input_shm_key));
	out->input_slots = in.input_slots;
	out->output_slots = in.output_slots;
	out->slot_data_bytes = in.slot_data_bytes;
	out->input_slot_data_bytes = in.input_slot_data_bytes;
}

void from_c(const oak_ipc_handshake *in, internal_ipc::HandshakeMsg *out)
{
	out->protocol_version = in->protocol_version;
	out->shm_key = QString::fromUtf8(in->shm_key);
	out->input_shm_key = QString::fromUtf8(in->input_shm_key);
	out->input_slots = in->input_slots;
	out->output_slots = in->output_slots;
	out->slot_data_bytes = in->slot_data_bytes;
	out->input_slot_data_bytes = in->input_slot_data_bytes;
}

void to_c(const internal_ipc::RenderFrameMsg &in, oak_ipc_render_frame *out)
{
	memset(out, 0, sizeof(*out));
	out->ticket_id = in.ticket_id;
	copy_to_buf(in.node_uuid, out->node_uuid, sizeof(out->node_uuid));
	out->time_num = in.time_num;
	out->time_den = in.time_den;
	out->width = in.width;
	out->height = in.height;
	out->format = in.format;
	out->channel_count = in.channel_count;
	out->mode = in.mode;
	out->input_slot = in.input_slot;
	const int count = qMin(int(in.input_slots.size()), OAK_IPC_INPUT_SLOTS_CAP);
	out->input_slot_count = count;
	for (int i = 0; i < count; i++) {
		out->input_slots[i] = in.input_slots.at(i);
	}
	out->has_color_transform = in.has_color_transform ? 1 : 0;
	out->color_is_display = in.color_is_display ? 1 : 0;
	copy_to_buf(in.color_output, out->color_output,
				sizeof(out->color_output));
	copy_to_buf(in.color_view, out->color_view, sizeof(out->color_view));
	copy_to_buf(in.color_look, out->color_look, sizeof(out->color_look));
}

void from_c(const oak_ipc_render_frame *in, internal_ipc::RenderFrameMsg *out)
{
	out->ticket_id = in->ticket_id;
	out->node_uuid = QString::fromUtf8(in->node_uuid);
	out->time_num = in->time_num;
	out->time_den = in->time_den;
	out->width = in->width;
	out->height = in->height;
	out->format = in->format;
	out->channel_count = in->channel_count;
	out->mode = in->mode;
	out->input_slot = in->input_slot;
	out->input_slots.clear();
	const int count = qMin(in->input_slot_count, OAK_IPC_INPUT_SLOTS_CAP);
	for (int i = 0; i < count; i++) {
		out->input_slots.append(in->input_slots[i]);
	}
	out->has_color_transform = in->has_color_transform != 0;
	out->color_is_display = in->color_is_display != 0;
	out->color_output = QString::fromUtf8(in->color_output);
	out->color_view = QString::fromUtf8(in->color_view);
	out->color_look = QString::fromUtf8(in->color_look);
}

void to_c(const internal_ipc::FrameReadyMsg &in, oak_ipc_frame_ready *out)
{
	memset(out, 0, sizeof(*out));
	out->ticket_id = in.ticket_id;
	out->output_slot = in.output_slot;
}

void from_c(const oak_ipc_frame_ready *in, internal_ipc::FrameReadyMsg *out)
{
	out->ticket_id = in->ticket_id;
	out->output_slot = in->output_slot;
}

void to_c(const internal_ipc::CancelMsg &in, oak_ipc_cancel *out)
{
	memset(out, 0, sizeof(*out));
	out->ticket_id = in.ticket_id;
}

void from_c(const oak_ipc_cancel *in, internal_ipc::CancelMsg *out)
{
	out->ticket_id = in->ticket_id;
}

void to_c(const internal_ipc::LoadGraphMsg &in, oak_ipc_load_graph *out)
{
	memset(out, 0, sizeof(*out));
	copy_to_buf(in.path, out->path, sizeof(out->path));
}

void from_c(const oak_ipc_load_graph *in, internal_ipc::LoadGraphMsg *out)
{
	out->path = QString::fromUtf8(in->path);
}

} // namespace

extern "C"
{

/* ---- SharedMemoryRegion ------------------------------------------------- */

OakSharedMemoryRegion *oakengine_ipc_shm_create(void)
{
	return wrap(new internal_ipc::SharedMemoryRegion());
}

void oakengine_ipc_shm_free(OakSharedMemoryRegion *self)
{
	delete impl(self);
}

int oakengine_ipc_shm_open(OakSharedMemoryRegion *self, const char *key,
						   size_t size, oak_ipc_shm_mode mode)
{
	if (!self || !key) {
		return 0;
	}
	const internal_ipc::SharedMemoryRegion::Mode m =
		mode == OAK_IPC_SHM_MODE_CREATE ?
			internal_ipc::SharedMemoryRegion::k_create :
			internal_ipc::SharedMemoryRegion::k_attach;
	return impl(self)->open(QString::fromUtf8(key), size, m) ? 1 : 0;
}

void oakengine_ipc_shm_close(OakSharedMemoryRegion *self)
{
	if (self) {
		impl(self)->close();
	}
}

int oakengine_ipc_shm_is_valid(const OakSharedMemoryRegion *self)
{
	return self && impl(self)->is_valid() ? 1 : 0;
}

void *oakengine_ipc_shm_data(OakSharedMemoryRegion *self)
{
	return self ? impl(self)->data() : nullptr;
}

size_t oakengine_ipc_shm_size(const OakSharedMemoryRegion *self)
{
	return self ? impl(self)->size() : 0;
}

int oakengine_ipc_shm_key(const OakSharedMemoryRegion *self, char *buf,
						  int buf_size)
{
	return string_to_buf(self ? impl(self)->key() : QString(), buf, buf_size);
}

int oakengine_ipc_shm_error(const OakSharedMemoryRegion *self, char *buf,
							int buf_size)
{
	return string_to_buf(self ? impl(self)->error() : QString(), buf, buf_size);
}

int oakengine_ipc_shm_make_key(int64_t owner_pid, int worker_index, char *buf,
							   int buf_size)
{
	return string_to_buf(
		internal_ipc::SharedMemoryRegion::make_key(owner_pid, worker_index),
		buf, buf_size);
}

/* ---- FrameSlotPool ------------------------------------------------------ */

size_t oakengine_ipc_framepool_bytes_needed(uint32_t slot_count,
											size_t slot_data_bytes)
{
	return internal_ipc::FrameSlotPool::bytes_needed(slot_count,
													 slot_data_bytes);
}

OakFrameSlotPool *oakengine_ipc_framepool_create(void *mem,
												 uint32_t slot_count,
												 size_t slot_data_bytes)
{
	if (!mem) {
		return nullptr;
	}
	return wrap(new internal_ipc::FrameSlotPool(
		internal_ipc::FrameSlotPool::create(mem, slot_count,
											slot_data_bytes)));
}

OakFrameSlotPool *oakengine_ipc_framepool_attach(void *mem)
{
	if (!mem) {
		return nullptr;
	}
	return wrap(new internal_ipc::FrameSlotPool(
		internal_ipc::FrameSlotPool::attach(mem)));
}

OakFrameSlotPool *oakengine_ipc_framepool_copy(const OakFrameSlotPool *self)
{
	if (!self) {
		return nullptr;
	}
	return wrap(new internal_ipc::FrameSlotPool(*impl(self)));
}

void oakengine_ipc_framepool_free(OakFrameSlotPool *self)
{
	delete impl(self);
}

int oakengine_ipc_framepool_is_valid(const OakFrameSlotPool *self)
{
	return self && impl(self)->is_valid() ? 1 : 0;
}

uint32_t oakengine_ipc_framepool_slot_count(const OakFrameSlotPool *self)
{
	return self ? impl(self)->slot_count() : 0;
}

size_t oakengine_ipc_framepool_slot_data_bytes(const OakFrameSlotPool *self)
{
	return self ? impl(self)->slot_data_bytes() : 0;
}

int oakengine_ipc_framepool_acquire(OakFrameSlotPool *self, uint32_t *index)
{
	return self && index && impl(self)->is_valid() &&
			   impl(self)->acquire(index) ?
			   1 :
			   0;
}

void *oakengine_ipc_framepool_slot_data(OakFrameSlotPool *self, uint32_t index)
{
	return self && impl(self)->is_valid() ? impl(self)->slot_data(index) :
											nullptr;
}

const void *oakengine_ipc_framepool_slot_data_const(
	const OakFrameSlotPool *self, uint32_t index)
{
	return self && impl(self)->is_valid() ? impl(self)->slot_data(index) :
											nullptr;
}

oak_frame_slot_meta *oakengine_ipc_framepool_meta(OakFrameSlotPool *self,
												  uint32_t index)
{
	return self && impl(self)->is_valid() ? impl(self)->meta(index) : nullptr;
}

const oak_frame_slot_meta *oakengine_ipc_framepool_meta_const(
	const OakFrameSlotPool *self, uint32_t index)
{
	return self && impl(self)->is_valid() ? impl(self)->meta(index) : nullptr;
}

int oakengine_ipc_framepool_publish(OakFrameSlotPool *self, uint32_t index)
{
	return self && impl(self)->is_valid() && impl(self)->publish(index) ? 1 : 0;
}

int oakengine_ipc_framepool_consume(OakFrameSlotPool *self, uint32_t *index)
{
	return self && index && impl(self)->is_valid() &&
			   impl(self)->consume(index) ?
			   1 :
			   0;
}

int oakengine_ipc_framepool_release(OakFrameSlotPool *self, uint32_t index)
{
	return self && impl(self)->is_valid() && impl(self)->release(index) ? 1 : 0;
}

/* ---- Control-plane messages --------------------------------------------- */

oak_ipc_msgtype oakengine_ipc_message_type(const char *json)
{
	QJsonObject o;
	if (!parse_object(json, &o)) {
		return OAK_IPC_MSGTYPE_UNKNOWN;
	}
	const QString type = o[QStringLiteral("type")].toString();
	if (type == QLatin1String(internal_ipc::msgtype::k_handshake)) {
		return OAK_IPC_MSGTYPE_HANDSHAKE;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_load_graph)) {
		return OAK_IPC_MSGTYPE_LOAD_GRAPH;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_render_frame)) {
		return OAK_IPC_MSGTYPE_RENDER_FRAME;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_frame_ready)) {
		return OAK_IPC_MSGTYPE_FRAME_READY;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_cancel)) {
		return OAK_IPC_MSGTYPE_CANCEL;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_graph_update)) {
		return OAK_IPC_MSGTYPE_GRAPH_UPDATE;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_shutdown)) {
		return OAK_IPC_MSGTYPE_SHUTDOWN;
	}
	if (type == QLatin1String(internal_ipc::msgtype::k_error)) {
		return OAK_IPC_MSGTYPE_ERROR;
	}
	return OAK_IPC_MSGTYPE_UNKNOWN;
}

int oakengine_ipc_handshake_to_json(const oak_ipc_handshake *self, char *buf,
									int buf_size)
{
	if (!self) {
		return -1;
	}
	internal_ipc::HandshakeMsg in;
	from_c(self, &in);
	return object_to_buf(in.to_json(), buf, buf_size);
}

int oakengine_ipc_handshake_parse(const char *json, oak_ipc_handshake *out)
{
	if (!out) {
		return 0;
	}
	QJsonObject o;
	internal_ipc::HandshakeMsg in;
	if (!parse_object(json, &o) ||
		!internal_ipc::HandshakeMsg::from_json(o, &in)) {
		return 0;
	}
	to_c(in, out);
	return 1;
}

int oakengine_ipc_render_frame_to_json(const oak_ipc_render_frame *self,
									   char *buf, int buf_size)
{
	if (!self) {
		return -1;
	}
	internal_ipc::RenderFrameMsg in;
	from_c(self, &in);
	return object_to_buf(in.to_json(), buf, buf_size);
}

int oakengine_ipc_render_frame_parse(const char *json,
									 oak_ipc_render_frame *out)
{
	if (!out) {
		return 0;
	}
	QJsonObject o;
	internal_ipc::RenderFrameMsg in;
	if (!parse_object(json, &o) ||
		!internal_ipc::RenderFrameMsg::from_json(o, &in)) {
		return 0;
	}
	to_c(in, out);
	return 1;
}

int oakengine_ipc_frame_ready_to_json(const oak_ipc_frame_ready *self,
									  char *buf, int buf_size)
{
	if (!self) {
		return -1;
	}
	internal_ipc::FrameReadyMsg in;
	from_c(self, &in);
	return object_to_buf(in.to_json(), buf, buf_size);
}

int oakengine_ipc_frame_ready_parse(const char *json,
									oak_ipc_frame_ready *out)
{
	if (!out) {
		return 0;
	}
	QJsonObject o;
	internal_ipc::FrameReadyMsg in;
	if (!parse_object(json, &o) ||
		!internal_ipc::FrameReadyMsg::from_json(o, &in)) {
		return 0;
	}
	to_c(in, out);
	return 1;
}

int oakengine_ipc_cancel_to_json(const oak_ipc_cancel *self, char *buf,
								 int buf_size)
{
	if (!self) {
		return -1;
	}
	internal_ipc::CancelMsg in;
	from_c(self, &in);
	return object_to_buf(in.to_json(), buf, buf_size);
}

int oakengine_ipc_cancel_parse(const char *json, oak_ipc_cancel *out)
{
	if (!out) {
		return 0;
	}
	QJsonObject o;
	internal_ipc::CancelMsg in;
	if (!parse_object(json, &o) ||
		!internal_ipc::CancelMsg::from_json(o, &in)) {
		return 0;
	}
	to_c(in, out);
	return 1;
}

int oakengine_ipc_load_graph_to_json(const oak_ipc_load_graph *self, char *buf,
									 int buf_size)
{
	if (!self) {
		return -1;
	}
	internal_ipc::LoadGraphMsg in;
	from_c(self, &in);
	return object_to_buf(in.to_json(), buf, buf_size);
}

int oakengine_ipc_load_graph_parse(const char *json, oak_ipc_load_graph *out)
{
	if (!out) {
		return 0;
	}
	QJsonObject o;
	internal_ipc::LoadGraphMsg in;
	if (!parse_object(json, &o) ||
		!internal_ipc::LoadGraphMsg::from_json(o, &in)) {
		return 0;
	}
	to_c(in, out);
	return 1;
}

int oakengine_ipc_shutdown_to_json(char *buf, int buf_size)
{
	QJsonObject o;
	o[QStringLiteral("type")] = internal_ipc::msgtype::k_shutdown;
	return object_to_buf(o, buf, buf_size);
}

int oakengine_ipc_shutdown_parse(const char *json)
{
	QJsonObject o;
	if (!parse_object(json, &o)) {
		return 0;
	}
	return o[QStringLiteral("type")].toString() ==
					   QLatin1String(internal_ipc::msgtype::k_shutdown) ?
			   1 :
			   0;
}

int oakengine_ipc_error_to_json(const char *message, char *buf, int buf_size)
{
	QJsonObject o;
	o[QStringLiteral("type")] = internal_ipc::msgtype::k_error;
	o[QStringLiteral("message")] = QString::fromUtf8(message ? message : "");
	return object_to_buf(o, buf, buf_size);
}

int oakengine_ipc_error_parse(const char *json, char *message_buf,
							  int message_buf_size)
{
	QJsonObject o;
	if (!parse_object(json, &o) ||
		o[QStringLiteral("type")].toString() !=
			QLatin1String(internal_ipc::msgtype::k_error)) {
		return 0;
	}
	if (message_buf && message_buf_size > 0) {
		copy_to_buf(o[QStringLiteral("message")].toString(), message_buf,
					size_t(message_buf_size));
	}
	return 1;
}

} // extern "C"
