// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Test-only inert shim for the oakengine_ipc_* C ABI (real implementation:
// engine/src/capi/ipc.cpp, still Qt-based and not buildable here).
// liboakrender references these symbols via -undefined dynamic_lookup; the
// oaknode test binary must provide them in the flat namespace so dyld can
// bind liboakrender at startup. The node-level tests never drive the render
// worker IPC paths, so every entry point fails/inert-safe: open returns 0,
// pools report invalid, acquire/consume report empty.

#include "oakengine/ipc.h"

#include <cstdio>
#include <cstring>

struct OakSharedMemoryRegion {
	int unused;
};

struct OakFrameSlotPool {
	int unused;
};

extern "C" {

OakSharedMemoryRegion *oakengine_ipc_shm_create(void)
{
	return new OakSharedMemoryRegion{};
}

void oakengine_ipc_shm_free(OakSharedMemoryRegion *self)
{
	delete self;
}

int oakengine_ipc_shm_open(OakSharedMemoryRegion *, const char *, size_t,
						   oak_ipc_shm_mode)
{
	return 0;
}

void oakengine_ipc_shm_close(OakSharedMemoryRegion *) {}

int oakengine_ipc_shm_is_valid(const OakSharedMemoryRegion *)
{
	return 0;
}

void *oakengine_ipc_shm_data(OakSharedMemoryRegion *)
{
	return nullptr;
}

size_t oakengine_ipc_shm_size(const OakSharedMemoryRegion *)
{
	return 0;
}

int oakengine_ipc_shm_key(const OakSharedMemoryRegion *, char *buf,
						  int buf_size)
{
	if (buf && buf_size > 0) {
		buf[0] = '\0';
	}
	return 0;
}

int oakengine_ipc_shm_error(const OakSharedMemoryRegion *, char *buf,
							int buf_size)
{
	static const char k_msg[] = "oakengine_ipc test shim: IPC unavailable";
	if (buf && buf_size > 0) {
		std::strncpy(buf, k_msg, size_t(buf_size) - 1);
		buf[buf_size - 1] = '\0';
	}
	return int(sizeof(k_msg) - 1);
}

int oakengine_ipc_shm_make_key(int64_t owner_pid, int worker_index, char *buf,
							   int buf_size)
{
	if (buf && buf_size > 0) {
		std::snprintf(buf, size_t(buf_size), "olive-rw-%lld-%d",
					  (long long) owner_pid, worker_index);
	}
	return 0;
}

size_t oakengine_ipc_framepool_bytes_needed(uint32_t, size_t)
{
	return 0;
}

OakFrameSlotPool *oakengine_ipc_framepool_create(void *, uint32_t, size_t)
{
	return nullptr;
}

OakFrameSlotPool *oakengine_ipc_framepool_attach(void *)
{
	return nullptr;
}

OakFrameSlotPool *oakengine_ipc_framepool_copy(const OakFrameSlotPool *)
{
	return nullptr;
}

void oakengine_ipc_framepool_free(OakFrameSlotPool *) {}

int oakengine_ipc_framepool_is_valid(const OakFrameSlotPool *)
{
	return 0;
}

uint32_t oakengine_ipc_framepool_slot_count(const OakFrameSlotPool *)
{
	return 0;
}

size_t oakengine_ipc_framepool_slot_data_bytes(const OakFrameSlotPool *)
{
	return 0;
}

int oakengine_ipc_framepool_acquire(OakFrameSlotPool *, uint32_t *)
{
	return 0;
}

void *oakengine_ipc_framepool_slot_data(OakFrameSlotPool *, uint32_t)
{
	return nullptr;
}

const void *oakengine_ipc_framepool_slot_data_const(const OakFrameSlotPool *,
													uint32_t)
{
	return nullptr;
}

oak_frame_slot_meta *oakengine_ipc_framepool_meta(OakFrameSlotPool *, uint32_t)
{
	return nullptr;
}

const oak_frame_slot_meta *
oakengine_ipc_framepool_meta_const(const OakFrameSlotPool *, uint32_t)
{
	return nullptr;
}

int oakengine_ipc_framepool_publish(OakFrameSlotPool *, uint32_t)
{
	return 0;
}

int oakengine_ipc_framepool_consume(OakFrameSlotPool *, uint32_t *)
{
	return 0;
}

int oakengine_ipc_framepool_release(OakFrameSlotPool *, uint32_t)
{
	return 0;
}

} // extern "C"
