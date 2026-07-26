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

// Pure C ABI test for the liboakengine render/ipc facade. Exercises the
// shared-memory region, the frame slot pool (including its version-1 wire
// layout) and every control-plane message build/parse pair. No Qt, no GL.

#include <assert.h>
#include <gtest/gtest.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "oakengine/ipc.h"

// ---- Version-1 wire layout: oak_frame_slot_meta must never move ----------
static_assert(sizeof(oak_frame_slot_meta) == 176, "FrameSlotMeta size");
static_assert(offsetof(oak_frame_slot_meta, id) == 0, "id offset");
static_assert(offsetof(oak_frame_slot_meta, time_num) == 8, "time_num offset");
static_assert(offsetof(oak_frame_slot_meta, time_den) == 16, "time_den offset");
static_assert(offsetof(oak_frame_slot_meta, width) == 24, "width offset");
static_assert(offsetof(oak_frame_slot_meta, height) == 28, "height offset");
static_assert(offsetof(oak_frame_slot_meta, format) == 32, "format offset");
static_assert(offsetof(oak_frame_slot_meta, channel_count) == 36,
			  "channel_count offset");
static_assert(offsetof(oak_frame_slot_meta, linesize) == 40, "linesize offset");
static_assert(offsetof(oak_frame_slot_meta, data_size) == 44,
			  "data_size offset");
static_assert(offsetof(oak_frame_slot_meta, colorspace) == 48,
			  "colorspace offset");

static void assert_json_eq(const char *produced, const char *expected)
{
	if (strcmp(produced, expected) != 0) {
		fprintf(stderr, "JSON mismatch:\n  produced: %s\n  expected: %s\n",
				produced, expected);
		EXPECT_TRUE(0);
	}
}

static void test_shm(void)
{
	// make_key: buf/size convention and exact format
	char key[OAK_IPC_SHM_KEY_CAP];
	const int64_t pid = getpid();
	int needed = oakengine_ipc_shm_make_key(pid, 0, NULL, 0);
	char expected[64];
	snprintf(expected, sizeof(expected), "olive-rw-%lld-0", (long long)pid);
	EXPECT_TRUE(needed == (int)strlen(expected));
	EXPECT_TRUE(oakengine_ipc_shm_make_key(pid, 0, key, sizeof(key)) == needed);
	EXPECT_TRUE(strcmp(key, expected) == 0);
	EXPECT_TRUE(oakengine_ipc_shm_make_key(pid, 0, key, 6) == needed); // truncated
	EXPECT_TRUE(strcmp(key, "olive") == 0);
	// A different worker index produces a different key.
	char key1[OAK_IPC_SHM_KEY_CAP];
	EXPECT_TRUE(oakengine_ipc_shm_make_key(pid, 1, key1, sizeof(key1)) == needed);
	EXPECT_TRUE(strcmp(key1, expected) != 0);

	const int key_len = oakengine_ipc_shm_make_key(pid, 0, key, sizeof(key));
	(void)key_len;

	// Attaching to a missing segment fails and reports an error.
	OakSharedMemoryRegion *missing = oakengine_ipc_shm_create();
	EXPECT_TRUE(oakengine_ipc_shm_open(missing, key, 4096,
								  OAK_IPC_SHM_MODE_ATTACH) == 0);
	EXPECT_TRUE(oakengine_ipc_shm_is_valid(missing) == 0);
	char err[256];
	EXPECT_TRUE(oakengine_ipc_shm_error(missing, err, sizeof(err)) > 0);
	EXPECT_TRUE(strlen(err) > 0);
	oakengine_ipc_shm_free(missing);

	// Create the owner: valid, sized, zero-initialized.
	OakSharedMemoryRegion *owner = oakengine_ipc_shm_create();
	EXPECT_TRUE(oakengine_ipc_shm_open(owner, key, 8192, OAK_IPC_SHM_MODE_CREATE) ==
		   1);
	EXPECT_TRUE(oakengine_ipc_shm_is_valid(owner) == 1);
	EXPECT_TRUE(oakengine_ipc_shm_size(owner) == 8192);
	unsigned char *data = (unsigned char *)oakengine_ipc_shm_data(owner);
	EXPECT_TRUE(data != NULL);
	for (int i = 0; i < 8192; i++) {
		EXPECT_TRUE(data[i] == 0);
	}

	// key() round-trips the opened key.
	char opened_key[OAK_IPC_SHM_KEY_CAP];
	EXPECT_TRUE(oakengine_ipc_shm_key(owner, opened_key, sizeof(opened_key)) ==
		   key_len);
	EXPECT_TRUE(strcmp(opened_key, key) == 0);

	// A peer attaches by the same key and sees the owner's bytes.
	data[0] = 0xAB;
	data[8191] = 0xCD;
	OakSharedMemoryRegion *peer = oakengine_ipc_shm_create();
	EXPECT_TRUE(oakengine_ipc_shm_open(peer, key, 8192, OAK_IPC_SHM_MODE_ATTACH) ==
		   1);
	EXPECT_TRUE(oakengine_ipc_shm_size(peer) == 8192);
	const unsigned char *peer_data =
		(const unsigned char *)oakengine_ipc_shm_data(peer);
	EXPECT_TRUE(peer_data[0] == 0xAB && peer_data[8191] == 0xCD);
	peer_data = NULL;

	// The peer closes without unlinking; the owner stays valid.
	oakengine_ipc_shm_close(peer);
	EXPECT_TRUE(oakengine_ipc_shm_is_valid(peer) == 0);
	EXPECT_TRUE(oakengine_ipc_shm_is_valid(owner) == 1);
	oakengine_ipc_shm_free(peer);

	// The owner unlinks on close: a late attach must fail.
	oakengine_ipc_shm_close(owner);
	EXPECT_TRUE(oakengine_ipc_shm_is_valid(owner) == 0);
	OakSharedMemoryRegion *late = oakengine_ipc_shm_create();
	EXPECT_TRUE(oakengine_ipc_shm_open(late, key, 8192, OAK_IPC_SHM_MODE_ATTACH) ==
		   0);
	oakengine_ipc_shm_free(late);
	oakengine_ipc_shm_free(owner);

	// NULL safety.
	oakengine_ipc_shm_close(NULL);
	EXPECT_TRUE(oakengine_ipc_shm_is_valid(NULL) == 0);
	EXPECT_TRUE(oakengine_ipc_shm_data(NULL) == NULL);
	EXPECT_TRUE(oakengine_ipc_shm_size(NULL) == 0);
	oakengine_ipc_shm_free(NULL);
}

static void test_framepool(void)
{
	const uint32_t k_slots = 3;
	const size_t k_slot_bytes = 256;

	// bytes_needed stays 64-aligned and grows with geometry.
	EXPECT_TRUE(oakengine_ipc_framepool_bytes_needed(k_slots, k_slot_bytes) % 64 ==
		   0);
	EXPECT_TRUE(oakengine_ipc_framepool_bytes_needed(2, 65) ==
		   oakengine_ipc_framepool_bytes_needed(2, 128));
	EXPECT_TRUE(oakengine_ipc_framepool_bytes_needed(1, 64) <
		   oakengine_ipc_framepool_bytes_needed(2, 64));

	const size_t bytes =
		oakengine_ipc_framepool_bytes_needed(k_slots, k_slot_bytes);
	void *mem = malloc(bytes);
	EXPECT_TRUE(mem != NULL);

	OakFrameSlotPool *filler =
		oakengine_ipc_framepool_create(mem, k_slots, k_slot_bytes);
	EXPECT_TRUE(filler != NULL);
	EXPECT_TRUE(oakengine_ipc_framepool_is_valid(filler) == 1);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_count(filler) == k_slots);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_data_bytes(filler) == k_slot_bytes);

	// Peer maps the same memory; a copy of the handle views it too.
	OakFrameSlotPool *drainer = oakengine_ipc_framepool_attach(mem);
	EXPECT_TRUE(oakengine_ipc_framepool_is_valid(drainer) == 1);
	OakFrameSlotPool *drainer_copy = oakengine_ipc_framepool_copy(drainer);
	EXPECT_TRUE(drainer_copy != NULL && drainer_copy != drainer);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_count(drainer_copy) == k_slots);

	// Free slots are issued FIFO; the pool then reports exhausted.
	uint32_t held[3];
	for (uint32_t i = 0; i < k_slots; i++) {
		EXPECT_TRUE(oakengine_ipc_framepool_acquire(filler, &held[i]) == 1);
		EXPECT_TRUE(held[i] == i);
	}
	uint32_t overflow = 99;
	EXPECT_TRUE(oakengine_ipc_framepool_acquire(filler, &overflow) == 0);
	EXPECT_TRUE(overflow == 99);

	// Fill slot 0 with a pattern + full metadata and publish it.
	unsigned char *data =
		(unsigned char *)oakengine_ipc_framepool_slot_data(filler, held[0]);
	EXPECT_TRUE(data != NULL);
	for (size_t i = 0; i < k_slot_bytes; i++) {
		data[i] = (unsigned char)(i & 0xFF);
	}
	oak_frame_slot_meta *meta = oakengine_ipc_framepool_meta(filler, held[0]);
	EXPECT_TRUE(meta != NULL);
	meta->id = -4242;
	meta->time_num = 1001;
	meta->time_den = 30000;
	meta->width = 3840;
	meta->height = 2160;
	meta->format = 17;
	meta->channel_count = 4;
	meta->linesize = 3840 * 4 * 4;
	meta->data_size = (int32_t)k_slot_bytes;
	strncpy(meta->colorspace, "acescg", sizeof(meta->colorspace) - 1);
	meta->colorspace[sizeof(meta->colorspace) - 1] = '\0';
	EXPECT_TRUE(oakengine_ipc_framepool_publish(filler, held[0]) == 1);

	// Publish slot 2 then slot 1: consume order follows publish order.
	EXPECT_TRUE(oakengine_ipc_framepool_publish(filler, held[2]) == 1);
	EXPECT_TRUE(oakengine_ipc_framepool_publish(filler, held[1]) == 1);

	uint32_t got = 99;
	EXPECT_TRUE(oakengine_ipc_framepool_consume(drainer, &got) == 1);
	EXPECT_TRUE(got == held[0]);

	// Metadata and pixels arrive intact on the drainer side.
	const oak_frame_slot_meta *out =
		oakengine_ipc_framepool_meta_const(drainer, got);
	EXPECT_TRUE(out != NULL);
	EXPECT_TRUE(out->id == -4242);
	EXPECT_TRUE(out->time_num == 1001 && out->time_den == 30000);
	EXPECT_TRUE(out->width == 3840 && out->height == 2160);
	EXPECT_TRUE(out->format == 17 && out->channel_count == 4);
	EXPECT_TRUE(out->linesize == 3840 * 4 * 4);
	EXPECT_TRUE(out->data_size == (int32_t)k_slot_bytes);
	EXPECT_TRUE(strcmp(out->colorspace, "acescg") == 0);
	const unsigned char *out_data =
		(const unsigned char *)oakengine_ipc_framepool_slot_data_const(drainer,
																	   got);
	EXPECT_TRUE(out_data != NULL);
	for (size_t i = 0; i < k_slot_bytes; i++) {
		EXPECT_TRUE(out_data[i] == (unsigned char)(i & 0xFF));
	}
	EXPECT_TRUE(oakengine_ipc_framepool_release(drainer, got) == 1);

	// The copied handle consumes the remaining slots in publish order.
	EXPECT_TRUE(oakengine_ipc_framepool_consume(drainer_copy, &got) == 1);
	EXPECT_TRUE(got == held[2]);
	EXPECT_TRUE(oakengine_ipc_framepool_release(drainer_copy, got) == 1);
	EXPECT_TRUE(oakengine_ipc_framepool_consume(drainer_copy, &got) == 1);
	EXPECT_TRUE(got == held[1]);
	EXPECT_TRUE(oakengine_ipc_framepool_release(drainer_copy, got) == 1);
	EXPECT_TRUE(oakengine_ipc_framepool_consume(drainer, &got) == 0); // drained

	// Released slots cycle back to the filler.
	uint32_t again = 0;
	EXPECT_TRUE(oakengine_ipc_framepool_acquire(filler, &again) == 1);

	oakengine_ipc_framepool_free(drainer_copy);
	oakengine_ipc_framepool_free(drainer);
	oakengine_ipc_framepool_free(filler);
	free(mem);

	// A region without the pool magic attaches as invalid.
	void *raw = calloc(1, oakengine_ipc_framepool_bytes_needed(2, 64));
	EXPECT_TRUE(raw != NULL);
	OakFrameSlotPool *bad = oakengine_ipc_framepool_attach(raw);
	EXPECT_TRUE(bad != NULL);
	EXPECT_TRUE(oakengine_ipc_framepool_is_valid(bad) == 0);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_count(bad) == 0);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_data_bytes(bad) == 0);
	oakengine_ipc_framepool_free(bad);
	free(raw);

	// NULL safety.
	EXPECT_TRUE(oakengine_ipc_framepool_is_valid(NULL) == 0);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_count(NULL) == 0);
	EXPECT_TRUE(oakengine_ipc_framepool_slot_data(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_ipc_framepool_meta(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_ipc_framepool_acquire(NULL, &again) == 0);
	oakengine_ipc_framepool_free(NULL);
}

// Frame hand-off across two live shared-memory mappings, the app<->worker shape.
static void test_framepool_over_shm(void)
{
	char key[OAK_IPC_SHM_KEY_CAP];
	EXPECT_TRUE(oakengine_ipc_shm_make_key(getpid(), 7, key, sizeof(key)) > 0);

	const uint32_t k_slots = 2;
	const size_t k_slot_bytes = 64;
	const size_t bytes =
		oakengine_ipc_framepool_bytes_needed(k_slots, k_slot_bytes);

	OakSharedMemoryRegion *owner = oakengine_ipc_shm_create();
	EXPECT_TRUE(oakengine_ipc_shm_open(owner, key, bytes, OAK_IPC_SHM_MODE_CREATE) ==
		   1);
	OakSharedMemoryRegion *peer = oakengine_ipc_shm_create();
	EXPECT_TRUE(oakengine_ipc_shm_open(peer, key, bytes, OAK_IPC_SHM_MODE_ATTACH) ==
		   1);

	OakFrameSlotPool *filler = oakengine_ipc_framepool_create(
		oakengine_ipc_shm_data(owner), k_slots, k_slot_bytes);
	OakFrameSlotPool *drainer = oakengine_ipc_framepool_attach(
		oakengine_ipc_shm_data(peer));
	EXPECT_TRUE(oakengine_ipc_framepool_is_valid(filler) == 1);
	EXPECT_TRUE(oakengine_ipc_framepool_is_valid(drainer) == 1);

	uint32_t idx = 0;
	EXPECT_TRUE(oakengine_ipc_framepool_acquire(filler, &idx) == 1);
	oak_frame_slot_meta *meta = oakengine_ipc_framepool_meta(filler, idx);
	meta->id = 77;
	meta->data_size = (int32_t)k_slot_bytes;
	memset(oakengine_ipc_framepool_slot_data(filler, idx), 0x5A, k_slot_bytes);
	EXPECT_TRUE(oakengine_ipc_framepool_publish(filler, idx) == 1);

	uint32_t got = 0;
	EXPECT_TRUE(oakengine_ipc_framepool_consume(drainer, &got) == 1);
	EXPECT_TRUE(got == idx);
	EXPECT_TRUE(oakengine_ipc_framepool_meta_const(drainer, got)->id == 77);
	const unsigned char *d = (const unsigned char *)
		oakengine_ipc_framepool_slot_data_const(drainer, got);
	EXPECT_TRUE(d[0] == 0x5A && d[k_slot_bytes - 1] == 0x5A);
	EXPECT_TRUE(oakengine_ipc_framepool_release(drainer, got) == 1);

	oakengine_ipc_framepool_free(drainer);
	oakengine_ipc_framepool_free(filler);
	oakengine_ipc_shm_free(peer);
	oakengine_ipc_shm_free(owner); // owner frees last: unlinks the segment
}

static void test_handshake(void)
{
	oak_ipc_handshake hs;
	memset(&hs, 0, sizeof(hs));
	hs.protocol_version = 1;
	strcpy(hs.shm_key, "olive-rw-1234-0-out");
	strcpy(hs.input_shm_key, "olive-rw-1234-1-in");
	hs.input_slots = 4;
	hs.output_slots = 6;
	hs.slot_data_bytes = 256ll * 1024 * 1024;
	hs.input_slot_data_bytes = 128ll * 1024 * 1024;

	const int needed = oakengine_ipc_handshake_to_json(&hs, NULL, 0);
	EXPECT_TRUE(needed > 0);
	char *json = (char *)malloc(size_t(needed) + 1);
	EXPECT_TRUE(json != NULL);
	EXPECT_TRUE(oakengine_ipc_handshake_to_json(&hs, json, needed + 1) == needed);
	EXPECT_TRUE((int)strlen(json) == needed);

	// buf/size convention: a short buffer truncates but reports the full size.
	char tiny[8];
	EXPECT_TRUE(oakengine_ipc_handshake_to_json(&hs, tiny, sizeof(tiny)) == needed);
	EXPECT_TRUE(strlen(tiny) == sizeof(tiny) - 1);

	EXPECT_TRUE(oakengine_ipc_message_type(json) == OAK_IPC_MSGTYPE_HANDSHAKE);

	oak_ipc_handshake back;
	EXPECT_TRUE(oakengine_ipc_handshake_parse(json, &back) == 1);
	EXPECT_TRUE(back.protocol_version == hs.protocol_version);
	EXPECT_TRUE(strcmp(back.shm_key, hs.shm_key) == 0);
	EXPECT_TRUE(strcmp(back.input_shm_key, hs.input_shm_key) == 0);
	EXPECT_TRUE(back.input_slots == hs.input_slots);
	EXPECT_TRUE(back.output_slots == hs.output_slots);
	EXPECT_TRUE(back.slot_data_bytes == hs.slot_data_bytes);
	EXPECT_TRUE(back.input_slot_data_bytes == hs.input_slot_data_bytes);
	free(json);

	// Exact wire text: same field names and compact shape the Qt builder emits.
	oak_ipc_handshake small;
	memset(&small, 0, sizeof(small));
	small.protocol_version = 1;
	strcpy(small.shm_key, "out");
	strcpy(small.input_shm_key, "in");
	small.input_slots = 4;
	small.output_slots = 6;
	small.slot_data_bytes = 256;
	small.input_slot_data_bytes = 128;
	const int n2 = oakengine_ipc_handshake_to_json(&small, NULL, 0);
	char *json2 = (char *)malloc(size_t(n2) + 1);
	EXPECT_TRUE(json2 != NULL);
	EXPECT_TRUE(oakengine_ipc_handshake_to_json(&small, json2, n2 + 1) == n2);
	assert_json_eq(json2,
				   "{\"input_shm_key\":\"in\",\"input_slot_data_bytes\":128,"
				   "\"input_slots\":4,\"output_slots\":6,\"protocol_version\":1,"
				   "\"shm_key\":\"out\",\"slot_data_bytes\":256,"
				   "\"type\":\"handshake\"}");
	free(json2);

	EXPECT_TRUE(oakengine_ipc_handshake_to_json(NULL, NULL, 0) == -1);
	EXPECT_TRUE(oakengine_ipc_handshake_parse("not json", &back) == 0);
	EXPECT_TRUE(oakengine_ipc_handshake_parse(NULL, &back) == 0);
}

static void test_render_frame(void)
{
	oak_ipc_render_frame rf;
	memset(&rf, 0, sizeof(rf));
	rf.ticket_id = (int64_t(1) << 52) + 12345; // exact as a JSON double
	strcpy(rf.node_uuid, "{abcd-1234}");
	rf.time_num = (int64_t)48000 * 123456789;
	rf.time_den = int64_t(1) << 40;
	rf.width = 1920;
	rf.height = 1080;
	rf.format = 3;
	rf.channel_count = 4;
	rf.mode = 1;
	rf.input_slot = 2;
	rf.input_slots[0] = 2;
	rf.input_slots[1] = 3;
	rf.input_slot_count = 2;
	rf.has_color_transform = 1;
	rf.color_is_display = 1;
	strcpy(rf.color_output, "sRGB - Display");
	strcpy(rf.color_view, "ACES 1.0 SDR-video");
	strcpy(rf.color_look, "None");

	const int needed = oakengine_ipc_render_frame_to_json(&rf, NULL, 0);
	char *json = (char *)malloc(size_t(needed) + 1);
	EXPECT_TRUE(json != NULL);
	EXPECT_TRUE(oakengine_ipc_render_frame_to_json(&rf, json, needed + 1) == needed);
	EXPECT_TRUE(oakengine_ipc_message_type(json) == OAK_IPC_MSGTYPE_RENDER_FRAME);

	oak_ipc_render_frame back;
	EXPECT_TRUE(oakengine_ipc_render_frame_parse(json, &back) == 1);
	EXPECT_TRUE(back.ticket_id == rf.ticket_id);
	EXPECT_TRUE(strcmp(back.node_uuid, rf.node_uuid) == 0);
	EXPECT_TRUE(back.time_num == rf.time_num);
	EXPECT_TRUE(back.time_den == rf.time_den);
	EXPECT_TRUE(back.width == rf.width && back.height == rf.height);
	EXPECT_TRUE(back.format == rf.format && back.channel_count == rf.channel_count);
	EXPECT_TRUE(back.mode == rf.mode);
	EXPECT_TRUE(back.input_slot == rf.input_slot);
	EXPECT_TRUE(back.input_slot_count == 2);
	EXPECT_TRUE(back.input_slots[0] == 2 && back.input_slots[1] == 3);
	EXPECT_TRUE(back.has_color_transform == 1 && back.color_is_display == 1);
	EXPECT_TRUE(strcmp(back.color_output, rf.color_output) == 0);
	EXPECT_TRUE(strcmp(back.color_view, rf.color_view) == 0);
	EXPECT_TRUE(strcmp(back.color_look, rf.color_look) == 0);
	free(json);

	// Without a color transform the color keys are omitted from the wire text.
	oak_ipc_render_frame plain;
	memset(&plain, 0, sizeof(plain));
	plain.ticket_id = 99;
	strcpy(plain.node_uuid, "{abcd-1234}");
	plain.time_num = 1001;
	plain.time_den = 30000;
	plain.width = 1920;
	plain.height = 1080;
	plain.format = 3;
	plain.channel_count = 4;
	plain.mode = 1;
	plain.input_slot = 2;
	plain.input_slots[0] = 2;
	plain.input_slots[1] = 3;
	plain.input_slot_count = 2;
	const int n2 = oakengine_ipc_render_frame_to_json(&plain, NULL, 0);
	char *json2 = (char *)malloc(size_t(n2) + 1);
	EXPECT_TRUE(json2 != NULL);
	EXPECT_TRUE(oakengine_ipc_render_frame_to_json(&plain, json2, n2 + 1) == n2);
	EXPECT_TRUE(strstr(json2, "has_color_transform") == NULL);
	assert_json_eq(json2,
				   "{\"channels\":4,\"format\":3,\"height\":1080,"
				   "\"input_slot\":2,\"input_slots\":[2,3],\"mode\":1,"
				   "\"node\":\"{abcd-1234}\",\"ticket\":99,\"time_den\":30000,"
				   "\"time_num\":1001,\"type\":\"render_frame\",\"width\":1920}");
	free(json2);

	// Defaults for a sparse message, mirroring the Qt from_json defaults.
	oak_ipc_render_frame sparse;
	EXPECT_TRUE(oakengine_ipc_render_frame_parse("{\"type\":\"render_frame\"}",
											&sparse) == 1);
	EXPECT_TRUE(sparse.ticket_id == 0);
	EXPECT_TRUE(sparse.time_num == 0 && sparse.time_den == 1);
	EXPECT_TRUE(sparse.width == 0 && sparse.height == 0);
	EXPECT_TRUE(sparse.format == -1 && sparse.channel_count == 0 && sparse.mode == 0);
	EXPECT_TRUE(sparse.input_slot == -1 && sparse.input_slot_count == 0);
	EXPECT_TRUE(sparse.has_color_transform == 0);

	// Legacy scalar input_slot folds into the array when it is absent.
	oak_ipc_render_frame legacy;
	EXPECT_TRUE(oakengine_ipc_render_frame_parse(
			   "{\"type\":\"render_frame\",\"ticket\":5,\"input_slot\":3}",
			   &legacy) == 1);
	EXPECT_TRUE(legacy.input_slot == 3);
	EXPECT_TRUE(legacy.input_slot_count == 1 && legacy.input_slots[0] == 3);

	// Type mismatches are rejected.
	oak_ipc_handshake hs;
	memset(&hs, 0, sizeof(hs));
	const int hn = oakengine_ipc_handshake_to_json(&hs, NULL, 0);
	char *hjson = (char *)malloc(size_t(hn) + 1);
	EXPECT_TRUE(hjson != NULL);
	EXPECT_TRUE(oakengine_ipc_handshake_to_json(&hs, hjson, hn + 1) == hn);
	EXPECT_TRUE(oakengine_ipc_render_frame_parse(hjson, &back) == 0);
	EXPECT_TRUE(oakengine_ipc_handshake_parse(hjson, &hs) == 1);
	free(hjson);
}

static void test_small_messages(void)
{
	// frame_ready
	oak_ipc_frame_ready fr;
	fr.ticket_id = 99;
	fr.output_slot = 2;
	const int fn = oakengine_ipc_frame_ready_to_json(&fr, NULL, 0);
	char *fjson = (char *)malloc(size_t(fn) + 1);
	EXPECT_TRUE(fjson != NULL);
	EXPECT_TRUE(oakengine_ipc_frame_ready_to_json(&fr, fjson, fn + 1) == fn);
	assert_json_eq(fjson, "{\"slot\":2,\"ticket\":99,\"type\":\"frame_ready\"}");
	EXPECT_TRUE(oakengine_ipc_message_type(fjson) == OAK_IPC_MSGTYPE_FRAME_READY);
	oak_ipc_frame_ready fr_back;
	EXPECT_TRUE(oakengine_ipc_frame_ready_parse(fjson, &fr_back) == 1);
	EXPECT_TRUE(fr_back.ticket_id == 99 && fr_back.output_slot == 2);
	free(fjson);

	// cancel
	oak_ipc_cancel cancel;
	cancel.ticket_id = 7;
	const int cn = oakengine_ipc_cancel_to_json(&cancel, NULL, 0);
	char *cjson = (char *)malloc(size_t(cn) + 1);
	EXPECT_TRUE(cjson != NULL);
	EXPECT_TRUE(oakengine_ipc_cancel_to_json(&cancel, cjson, cn + 1) == cn);
	assert_json_eq(cjson, "{\"ticket\":7,\"type\":\"cancel\"}");
	EXPECT_TRUE(oakengine_ipc_message_type(cjson) == OAK_IPC_MSGTYPE_CANCEL);
	oak_ipc_cancel cancel_back;
	EXPECT_TRUE(oakengine_ipc_cancel_parse(cjson, &cancel_back) == 1);
	EXPECT_TRUE(cancel_back.ticket_id == 7);
	// cancel rejects a frame_ready payload.
	EXPECT_TRUE(oakengine_ipc_cancel_parse("{\"slot\":2,\"ticket\":7,\"type\":"
									  "\"frame_ready\"}",
									  &cancel_back) == 0);
	free(cjson);

	// load_graph
	oak_ipc_load_graph load;
	strcpy(load.path, "/tmp/oak-render-graph-abc123.ove");
	const int ln = oakengine_ipc_load_graph_to_json(&load, NULL, 0);
	char *ljson = (char *)malloc(size_t(ln) + 1);
	EXPECT_TRUE(ljson != NULL);
	EXPECT_TRUE(oakengine_ipc_load_graph_to_json(&load, ljson, ln + 1) == ln);
	assert_json_eq(ljson,
				   "{\"path\":\"/tmp/oak-render-graph-abc123.ove\",\"type\":"
				   "\"load_graph\"}");
	EXPECT_TRUE(oakengine_ipc_message_type(ljson) == OAK_IPC_MSGTYPE_LOAD_GRAPH);
	oak_ipc_load_graph load_back;
	EXPECT_TRUE(oakengine_ipc_load_graph_parse(ljson, &load_back) == 1);
	EXPECT_TRUE(strcmp(load_back.path, load.path) == 0);
	free(ljson);

	// shutdown
	const int sn = oakengine_ipc_shutdown_to_json(NULL, 0);
	char *sjson = (char *)malloc(size_t(sn) + 1);
	EXPECT_TRUE(sjson != NULL);
	EXPECT_TRUE(oakengine_ipc_shutdown_to_json(sjson, sn + 1) == sn);
	assert_json_eq(sjson, "{\"type\":\"shutdown\"}");
	EXPECT_TRUE(oakengine_ipc_shutdown_parse(sjson) == 1);
	EXPECT_TRUE(oakengine_ipc_shutdown_parse("{\"type\":\"cancel\"}") == 0);
	EXPECT_TRUE(oakengine_ipc_message_type(sjson) == OAK_IPC_MSGTYPE_SHUTDOWN);
	free(sjson);

	// error
	const int en = oakengine_ipc_error_to_json("boom", NULL, 0);
	char *ejson = (char *)malloc(size_t(en) + 1);
	EXPECT_TRUE(ejson != NULL);
	EXPECT_TRUE(oakengine_ipc_error_to_json("boom", ejson, en + 1) == en);
	assert_json_eq(ejson, "{\"message\":\"boom\",\"type\":\"error\"}");
	EXPECT_TRUE(oakengine_ipc_message_type(ejson) == OAK_IPC_MSGTYPE_ERROR);
	char msg[OAK_IPC_ERROR_MESSAGE_CAP];
	EXPECT_TRUE(oakengine_ipc_error_parse(ejson, msg, sizeof(msg)) == 1);
	EXPECT_TRUE(strcmp(msg, "boom") == 0);
	EXPECT_TRUE(oakengine_ipc_error_parse("{\"type\":\"shutdown\"}", msg,
									 sizeof(msg)) == 0);
	free(ejson);

	// Unknown and malformed input.
	EXPECT_TRUE(oakengine_ipc_message_type("{\"type\":\"nope\"}") ==
		   OAK_IPC_MSGTYPE_UNKNOWN);
	EXPECT_TRUE(oakengine_ipc_message_type("garbage") == OAK_IPC_MSGTYPE_UNKNOWN);
	EXPECT_TRUE(oakengine_ipc_message_type(NULL) == OAK_IPC_MSGTYPE_UNKNOWN);
	EXPECT_TRUE(OAK_IPC_MSGTYPE_GRAPH_UPDATE != OAK_IPC_MSGTYPE_UNKNOWN);
}

TEST(OakEngineIpc, Main)
{
	test_shm();
	test_framepool();
	test_framepool_over_shm();
	test_handshake();
	test_render_frame();
	test_small_messages();

}
