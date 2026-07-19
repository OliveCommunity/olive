/*
 * Oak Video Editor - Render Worker Pool & IPC Coverage Tests
 * Copyright (C) 2026 Oak Team
 *
 * CPU-only, headless coverage for the render worker IPC stack that
 * render_ipc_test.cpp and render_worker_footage_test.cpp do not reach:
 * - SharedMemoryRegion  (named shared memory create/attach/lifetime)
 * - FrameSlotPool       (layout math, invalid attach, metadata fields, FIFO order)
 * - NDJSON messages     (color transform, legacy input_slot fallback, defaults,
 *                        blank/non-object lines, closed-device writes)
 * - RenderWorkerPool    (early validation paths that need no worker process)
 * - DecoderCache        (DecoderPair defaults and insert/value round trip)
 *
 * No worker processes, GPU, audio devices, or network access are required; the
 * shared-memory tests use real OS segments with per-run unique keys.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <QBuffer>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <olive/core/core.h>

#include "codec/decoder.h"
#include "node/output/track/track.h"
#include "render/ipc/frameslotpool.h"
#include "render/ipc/ipcmessage.h"
#include "render/ipc/sharedmemoryregion.h"
#include "render/rendercache.h"
#include "render/renderworkerpool.h"

namespace
{

// Unique-per-run segment key so stale POSIX segments left by earlier runs can
// never collide with a test (MakeKey() bakes the pid into the key).
QString test_shm_key(const char *tag)
{
	return olive::ipc::SharedMemoryRegion::make_key(
			   QCoreApplication::applicationPid(), 99) +
		   QStringLiteral("-") + QLatin1String(tag);
}

olive::RenderManager::RenderVideoParams
make_video_params(olive::Node *node, const olive::VideoParams &video_params)
{
	return olive::RenderManager::RenderVideoParams(
		node, video_params, olive::core::AudioParams(),
		olive::core::Rational(0), nullptr, olive::RenderMode::k_online);
}

} // namespace

// ============================================================================
// SharedMemoryRegion
// ============================================================================

TEST(SharedMemoryRegion, MakeKeyFormat)
{
	EXPECT_EQ(olive::ipc::SharedMemoryRegion::make_key(12345, 3),
			  QStringLiteral("olive-rw-12345-3"));

	EXPECT_NE(olive::ipc::SharedMemoryRegion::make_key(12345, 3),
			  olive::ipc::SharedMemoryRegion::make_key(12345, 4));
	EXPECT_NE(olive::ipc::SharedMemoryRegion::make_key(12345, 3),
			  olive::ipc::SharedMemoryRegion::make_key(12346, 3));
}

TEST(SharedMemoryRegion, CreateProvidesZeroedWritableMemory)
{
	const QString key = test_shm_key("zeroed");
	olive::ipc::SharedMemoryRegion region;
	ASSERT_TRUE(region.open(key, 4096, olive::ipc::SharedMemoryRegion::k_create))
		<< region.error().toStdString();

	EXPECT_TRUE(region.is_valid());
	EXPECT_EQ(region.size(), size_t(4096));
	EXPECT_EQ(region.key(), key);
	ASSERT_NE(region.data(), nullptr);

	// Freshly created segments are zero-filled.
	const auto *bytes = static_cast<const uint8_t *>(region.data());
	for (size_t i = 0; i < region.size(); i++) {
		ASSERT_EQ(bytes[i], 0) << "byte " << i;
	}

	// The mapping is readable and writable.
	auto *writable = static_cast<uint8_t *>(region.data());
	for (size_t i = 0; i < region.size(); i++) {
		writable[i] = uint8_t(i * 31 + 7);
	}
	EXPECT_EQ(writable[0], 7);
	EXPECT_EQ(writable[4095], uint8_t(4095 * 31 + 7));
}

TEST(SharedMemoryRegion, AttachToMissingKeyFails)
{
	olive::ipc::SharedMemoryRegion region;
	EXPECT_FALSE(region.open(test_shm_key("missing"), 4096,
							 olive::ipc::SharedMemoryRegion::k_attach));
	EXPECT_FALSE(region.is_valid());
	EXPECT_EQ(region.data(), nullptr);
	EXPECT_FALSE(region.error().isEmpty());
}

TEST(SharedMemoryRegion, CreateAttachRoundTrip)
{
	const QString key = test_shm_key("roundtrip");

	olive::ipc::SharedMemoryRegion owner;
	ASSERT_TRUE(owner.open(key, 8192, olive::ipc::SharedMemoryRegion::k_create))
		<< owner.error().toStdString();

	olive::ipc::SharedMemoryRegion peer;
	ASSERT_TRUE(peer.open(key, 8192, olive::ipc::SharedMemoryRegion::k_attach))
		<< peer.error().toStdString();
	EXPECT_TRUE(peer.is_valid());
	EXPECT_EQ(peer.size(), size_t(8192));
	EXPECT_EQ(peer.key(), key);

	// Writes through the owner mapping are visible through the peer mapping.
	auto *owner_bytes = static_cast<uint8_t *>(owner.data());
	const auto *peer_bytes = static_cast<const uint8_t *>(peer.data());
	for (size_t i = 0; i < 8192; i += 257) {
		owner_bytes[i] = uint8_t(i ^ 0x5A);
	}
	for (size_t i = 0; i < 8192; i += 257) {
		EXPECT_EQ(peer_bytes[i], uint8_t(i ^ 0x5A)) << "offset " << i;
	}

	// And vice versa: the peer maps the same segment read/write.
	auto *peer_writable = static_cast<uint8_t *>(peer.data());
	peer_writable[123] = 0xA5;
	EXPECT_EQ(owner_bytes[123], 0xA5);
}

TEST(SharedMemoryRegion, ZeroSizeCreateFails)
{
	olive::ipc::SharedMemoryRegion region;
	// A zero-length mapping is rejected (EINVAL from mmap on POSIX, invalid
	// size for CreateFileMapping on Windows).
	EXPECT_FALSE(region.open(test_shm_key("zerosize"), 0,
							 olive::ipc::SharedMemoryRegion::k_create));
	EXPECT_FALSE(region.is_valid());
	EXPECT_FALSE(region.error().isEmpty());
}

TEST(SharedMemoryRegion, CloseInvalidatesThenReopenWorks)
{
	olive::ipc::SharedMemoryRegion region;
	ASSERT_TRUE(region.open(test_shm_key("close1"), 4096,
							olive::ipc::SharedMemoryRegion::k_create));

	region.close();
	EXPECT_FALSE(region.is_valid());
	EXPECT_EQ(region.data(), nullptr);
	EXPECT_EQ(region.size(), size_t(0));

	// Close is idempotent.
	region.close();
	EXPECT_FALSE(region.is_valid());

	// The same object can be reused for a new segment (Open() closes first).
	ASSERT_TRUE(region.open(test_shm_key("close2"), 2048,
							olive::ipc::SharedMemoryRegion::k_create));
	EXPECT_TRUE(region.is_valid());
	EXPECT_EQ(region.size(), size_t(2048));
}

TEST(SharedMemoryRegion, OwnerDestructionUnlinksSegment)
{
	const QString key = test_shm_key("unlink");
	{
		olive::ipc::SharedMemoryRegion owner;
		ASSERT_TRUE(owner.open(key, 4096,
							   olive::ipc::SharedMemoryRegion::k_create));

		// While the owner lives, attaching works.
		olive::ipc::SharedMemoryRegion peer;
		ASSERT_TRUE(
			peer.open(key, 4096, olive::ipc::SharedMemoryRegion::k_attach));
	}

	// Once the owner is destroyed the name is unlinked; new attaches fail.
	olive::ipc::SharedMemoryRegion late;
	EXPECT_FALSE(late.open(key, 4096, olive::ipc::SharedMemoryRegion::k_attach));
	EXPECT_FALSE(late.is_valid());
}

// ============================================================================
// FrameSlotPool
// ============================================================================

TEST(FrameSlotPool, AttachRejectsBadMagic)
{
	// A region that was never initialized by Create() has no valid magic number.
	std::vector<uint8_t> mem(olive::ipc::FrameSlotPool::bytes_needed(2, 64), 0xAB);
	olive::ipc::FrameSlotPool pool = olive::ipc::FrameSlotPool::attach(mem.data());

	EXPECT_FALSE(pool.is_valid());
	EXPECT_EQ(pool.slot_count(), 0u);
	EXPECT_EQ(pool.slot_data_bytes(), size_t(0));
}

TEST(FrameSlotPool, DefaultConstructedIsInvalid)
{
	olive::ipc::FrameSlotPool pool;
	EXPECT_FALSE(pool.is_valid());
	EXPECT_EQ(pool.slot_count(), 0u);
	EXPECT_EQ(pool.slot_data_bytes(), size_t(0));
}

TEST(FrameSlotPool, BytesNeededReflectsGeometry)
{
	// The total is a sum of 64-byte-aligned sub-regions, so it stays 64-aligned.
	EXPECT_EQ(olive::ipc::FrameSlotPool::bytes_needed(1, 64) % 64, 0u);
	EXPECT_EQ(olive::ipc::FrameSlotPool::bytes_needed(3, 1000) % 64, 0u);

	// More slots and bigger slots both need strictly more memory...
	EXPECT_LT(olive::ipc::FrameSlotPool::bytes_needed(1, 64),
			  olive::ipc::FrameSlotPool::bytes_needed(2, 64));
	EXPECT_LT(olive::ipc::FrameSlotPool::bytes_needed(2, 64),
			  olive::ipc::FrameSlotPool::bytes_needed(3, 64));
	EXPECT_LT(olive::ipc::FrameSlotPool::bytes_needed(2, 64),
			  olive::ipc::FrameSlotPool::bytes_needed(2, 128));

	// ...but sizes inside the same 64-byte alignment bucket collapse together.
	EXPECT_EQ(olive::ipc::FrameSlotPool::bytes_needed(2, 65),
			  olive::ipc::FrameSlotPool::bytes_needed(2, 128));
}

TEST(FrameSlotPool, SlotDataBlocksAreAlignedAndDistinct)
{
	constexpr uint32_t k_slots = 2;
	constexpr size_t k_slot_bytes = 100; // deliberately not 64-aligned

	std::vector<uint8_t> mem(
		olive::ipc::FrameSlotPool::bytes_needed(k_slots, k_slot_bytes));
	olive::ipc::FrameSlotPool pool =
		olive::ipc::FrameSlotPool::create(mem.data(), k_slots, k_slot_bytes);
	ASSERT_TRUE(pool.is_valid());

	auto *first = static_cast<uint8_t *>(pool.slot_data(0));
	auto *second = static_cast<uint8_t *>(pool.slot_data(1));

	// Slot data blocks are padded out to 64-byte boundaries within the region.
	EXPECT_EQ(second - first, ptrdiff_t(128));

	// A full-size write to one slot never spills into the next.
	std::memset(first, 0x11, k_slot_bytes);
	std::memset(second, 0x22, k_slot_bytes);
	EXPECT_EQ(first[k_slot_bytes - 1], 0x11);
	EXPECT_EQ(second[0], 0x22);

	// The const overload maps the same addresses.
	const olive::ipc::FrameSlotPool &const_pool = pool;
	EXPECT_EQ(static_cast<const uint8_t *>(const_pool.slot_data(0)), first);
	EXPECT_EQ(static_cast<const uint8_t *>(const_pool.slot_data(1)), second);
}

TEST(FrameSlotPool, MetadataFieldsRoundTrip)
{
	constexpr uint32_t k_slots = 2;
	constexpr size_t k_slot_bytes = 64;

	std::vector<uint8_t> mem(
		olive::ipc::FrameSlotPool::bytes_needed(k_slots, k_slot_bytes));
	olive::ipc::FrameSlotPool filler =
		olive::ipc::FrameSlotPool::create(mem.data(), k_slots, k_slot_bytes);
	olive::ipc::FrameSlotPool drainer =
		olive::ipc::FrameSlotPool::attach(mem.data());

	uint32_t idx = 0;
	ASSERT_TRUE(filler.acquire(&idx));

	// Freshly created pools zero the metadata array.
	const olive::ipc::FrameSlotPool &const_drainer = drainer;
	const olive::ipc::FrameSlotMeta *blank = const_drainer.meta(idx);
	EXPECT_EQ(blank->id, 0);
	EXPECT_EQ(blank->time_num, 0);
	EXPECT_EQ(blank->time_den, 0);
	EXPECT_EQ(blank->width, 0);
	EXPECT_EQ(blank->colorspace[0], '\0');

	// Every field the producer writes survives the hand-off.
	olive::ipc::FrameSlotMeta *meta = filler.meta(idx);
	meta->id = -99;
	meta->time_num = 1001;
	meta->time_den = 30000;
	meta->width = 3840;
	meta->height = 2160;
	meta->format = int(olive::core::PixelFormat::f32);
	meta->channel_count = 4;
	meta->linesize = 3840 * 4 * 4;
	meta->data_size = int32_t(k_slot_bytes);
	const char k_colorspace[] = "acescg";
	std::strncpy(meta->colorspace, k_colorspace, sizeof(meta->colorspace) - 1);
	meta->colorspace[sizeof(meta->colorspace) - 1] = '\0';

	ASSERT_TRUE(filler.publish(idx));

	uint32_t got = 0;
	ASSERT_TRUE(drainer.consume(&got));
	EXPECT_EQ(got, idx);

	const olive::ipc::FrameSlotMeta *out = const_drainer.meta(got);
	EXPECT_EQ(out->id, -99);
	EXPECT_EQ(out->time_num, 1001);
	EXPECT_EQ(out->time_den, 30000);
	EXPECT_EQ(out->width, 3840);
	EXPECT_EQ(out->height, 2160);
	EXPECT_EQ(out->format, int(olive::core::PixelFormat::f32));
	EXPECT_EQ(out->channel_count, 4);
	EXPECT_EQ(out->linesize, 3840 * 4 * 4);
	EXPECT_EQ(out->data_size, int32_t(k_slot_bytes));
	EXPECT_STREQ(out->colorspace, k_colorspace);

	EXPECT_TRUE(drainer.release(got));
}

TEST(FrameSlotPool, FreeSlotsAreIssuedInOrder)
{
	constexpr uint32_t k_slots = 4;
	std::vector<uint8_t> mem(
		olive::ipc::FrameSlotPool::bytes_needed(k_slots, 64));
	olive::ipc::FrameSlotPool pool =
		olive::ipc::FrameSlotPool::create(mem.data(), k_slots, 64);

	// Create() seeds the free ring FIFO with every slot index.
	for (uint32_t expected = 0; expected < k_slots; expected++) {
		uint32_t idx = 0;
		ASSERT_TRUE(pool.acquire(&idx));
		EXPECT_EQ(idx, expected);
	}

	uint32_t overflow = 0;
	EXPECT_FALSE(pool.acquire(&overflow));

	// Released slots are re-issued in the order they were released.
	ASSERT_TRUE(pool.release(2));
	ASSERT_TRUE(pool.release(0));
	uint32_t idx = 0;
	ASSERT_TRUE(pool.acquire(&idx));
	EXPECT_EQ(idx, 2u);
	ASSERT_TRUE(pool.acquire(&idx));
	EXPECT_EQ(idx, 0u);
}

TEST(FrameSlotPool, ReadyRingDeliversInPublishOrder)
{
	constexpr uint32_t k_slots = 3;
	std::vector<uint8_t> mem(
		olive::ipc::FrameSlotPool::bytes_needed(k_slots, 64));
	olive::ipc::FrameSlotPool pool =
		olive::ipc::FrameSlotPool::create(mem.data(), k_slots, 64);

	uint32_t a = 0, b = 0, c = 0;
	ASSERT_TRUE(pool.acquire(&a));
	ASSERT_TRUE(pool.acquire(&b));
	ASSERT_TRUE(pool.acquire(&c));

	// Publish order, not slot order, determines consume order.
	ASSERT_TRUE(pool.publish(c));
	ASSERT_TRUE(pool.publish(a));
	ASSERT_TRUE(pool.publish(b));

	const uint32_t expected[] = { c, a, b };
	for (uint32_t want : expected) {
		uint32_t got = 0;
		ASSERT_TRUE(pool.consume(&got));
		EXPECT_EQ(got, want);
		ASSERT_TRUE(pool.release(got));
	}

	uint32_t empty = 0;
	EXPECT_FALSE(pool.consume(&empty));
}

TEST(FrameSlotPool, CrossMappingHandoff)
{
	constexpr uint32_t k_slots = 2;
	constexpr size_t k_slot_bytes = 128;
	const QString key = test_shm_key("pool-handoff");

	const size_t bytes =
		olive::ipc::FrameSlotPool::bytes_needed(k_slots, k_slot_bytes);

	olive::ipc::SharedMemoryRegion owner_region;
	ASSERT_TRUE(owner_region.open(key, bytes,
								  olive::ipc::SharedMemoryRegion::k_create))
		<< owner_region.error().toStdString();
	olive::ipc::FrameSlotPool filler = olive::ipc::FrameSlotPool::create(
		owner_region.data(), k_slots, k_slot_bytes);
	ASSERT_TRUE(filler.is_valid());

	// The peer maps the same segment separately and attaches to the pool header.
	olive::ipc::SharedMemoryRegion peer_region;
	ASSERT_TRUE(peer_region.open(key, bytes,
								 olive::ipc::SharedMemoryRegion::k_attach))
		<< peer_region.error().toStdString();
	olive::ipc::FrameSlotPool drainer =
		olive::ipc::FrameSlotPool::attach(peer_region.data());
	ASSERT_TRUE(drainer.is_valid());
	EXPECT_EQ(drainer.slot_count(), k_slots);
	EXPECT_EQ(drainer.slot_data_bytes(), k_slot_bytes);

	// Filler side: acquire a slot, stamp it, publish it.
	uint32_t idx = 0;
	ASSERT_TRUE(filler.acquire(&idx));
	auto *data = static_cast<uint8_t *>(filler.slot_data(idx));
	for (size_t i = 0; i < k_slot_bytes; i++) {
		data[i] = uint8_t(0xC3 ^ i);
	}
	filler.meta(idx)->id = 777;
	ASSERT_TRUE(filler.publish(idx));

	// Drainer side (through the second mapping): same slot, meta and pixels.
	uint32_t got = 0;
	ASSERT_TRUE(drainer.consume(&got));
	EXPECT_EQ(got, idx);
	EXPECT_EQ(drainer.meta(got)->id, 777);
	const auto *peer_data =
		static_cast<const uint8_t *>(drainer.slot_data(got));
	for (size_t i = 0; i < k_slot_bytes; i++) {
		ASSERT_EQ(peer_data[i], uint8_t(0xC3 ^ i)) << "byte " << i;
	}
	ASSERT_TRUE(drainer.release(got));

	// The release crosses back to the owner's mapping. The free ring is FIFO:
	// the next fresh slot comes first, then the released slot cycles back.
	uint32_t reacquired = 0;
	ASSERT_TRUE(filler.acquire(&reacquired));
	EXPECT_EQ(reacquired, 1u);
	ASSERT_TRUE(filler.acquire(&reacquired));
	EXPECT_EQ(reacquired, got);
}

// ============================================================================
// NDJSON control messages (beyond render_ipc_test.cpp)
// ============================================================================

TEST(IpcMessage, LoadGraphRoundTrip)
{
	olive::ipc::LoadGraphMsg msg;
	msg.path = QStringLiteral("/tmp/oak-render-graph-abc123.ove");

	const QJsonObject obj = msg.to_json();
	EXPECT_EQ(obj.value(QStringLiteral("type")).toString(),
			  QLatin1String(olive::ipc::msgtype::k_load_graph));

	olive::ipc::LoadGraphMsg back;
	ASSERT_TRUE(olive::ipc::LoadGraphMsg::from_json(obj, &back));
	EXPECT_EQ(back.path, msg.path);
}

TEST(IpcMessage, RenderFrameColorTransformRoundTrip)
{
	olive::ipc::RenderFrameMsg msg;
	msg.ticket_id = 123;
	msg.node_uuid = QStringLiteral("{11111111-2222-3333-4444-555555555555}");
	msg.time_num = 1001;
	msg.time_den = 24000;
	msg.width = 1920;
	msg.height = 1080;
	msg.format = int(olive::core::PixelFormat::f32);
	msg.channel_count = 4;
	msg.mode = 1;
	msg.input_slots = { 0, 2, 5 };
	msg.has_color_transform = true;
	msg.color_is_display = true;
	msg.color_output = QStringLiteral("sRGB - Display");
	msg.color_view = QStringLiteral("ACES 1.0 SDR-video");
	msg.color_look = QStringLiteral("None");

	const QJsonObject obj = msg.to_json();
	EXPECT_EQ(obj.value(QStringLiteral("type")).toString(),
			  QLatin1String(olive::ipc::msgtype::k_render_frame));
	EXPECT_TRUE(obj.value(QStringLiteral("has_color_transform")).toBool());

	olive::ipc::RenderFrameMsg back;
	ASSERT_TRUE(olive::ipc::RenderFrameMsg::from_json(obj, &back));
	EXPECT_EQ(back.ticket_id, msg.ticket_id);
	EXPECT_EQ(back.node_uuid, msg.node_uuid);
	EXPECT_EQ(back.time_num, msg.time_num);
	EXPECT_EQ(back.time_den, msg.time_den);
	EXPECT_EQ(back.width, msg.width);
	EXPECT_EQ(back.height, msg.height);
	EXPECT_EQ(back.format, msg.format);
	EXPECT_EQ(back.channel_count, msg.channel_count);
	EXPECT_EQ(back.mode, msg.mode);
	EXPECT_EQ(back.input_slots, msg.input_slots);
	EXPECT_TRUE(back.has_color_transform);
	EXPECT_TRUE(back.color_is_display);
	EXPECT_EQ(back.color_output, msg.color_output);
	EXPECT_EQ(back.color_view, msg.color_view);
	EXPECT_EQ(back.color_look, msg.color_look);
}

TEST(IpcMessage, RenderFrameOmitsColorTransformWhenUnset)
{
	olive::ipc::RenderFrameMsg msg; // has_color_transform defaults to false
	const QJsonObject obj = msg.to_json();

	EXPECT_FALSE(obj.contains(QStringLiteral("has_color_transform")));
	EXPECT_FALSE(obj.contains(QStringLiteral("color_output")));
	EXPECT_FALSE(obj.contains(QStringLiteral("color_view")));
	EXPECT_FALSE(obj.contains(QStringLiteral("color_look")));

	olive::ipc::RenderFrameMsg back;
	ASSERT_TRUE(olive::ipc::RenderFrameMsg::from_json(obj, &back));
	EXPECT_FALSE(back.has_color_transform);
	EXPECT_FALSE(back.color_is_display);
	EXPECT_TRUE(back.color_output.isEmpty());
}

TEST(IpcMessage, RenderFrameLegacyInputSlotFallback)
{
	// Older peers only send the scalar "input_slot"; FromJson folds it into the
	// input_slots array when the array is absent.
	QJsonObject obj;
	obj[QStringLiteral("type")] =
		QLatin1String(olive::ipc::msgtype::k_render_frame);
	obj[QStringLiteral("ticket")] = 5.0;
	obj[QStringLiteral("input_slot")] = 3;

	olive::ipc::RenderFrameMsg back;
	ASSERT_TRUE(olive::ipc::RenderFrameMsg::from_json(obj, &back));
	EXPECT_EQ(back.input_slot, 3);
	ASSERT_EQ(back.input_slots.size(), 1);
	EXPECT_EQ(back.input_slots.first(), 3);

	// When the array is present it wins and the scalar is not duplicated.
	obj[QStringLiteral("input_slots")] = QJsonArray{ 7, 8 };
	olive::ipc::RenderFrameMsg back2;
	ASSERT_TRUE(olive::ipc::RenderFrameMsg::from_json(obj, &back2));
	ASSERT_EQ(back2.input_slots.size(), 2);
	EXPECT_EQ(back2.input_slots.at(0), 7);
	EXPECT_EQ(back2.input_slots.at(1), 8);
}

TEST(IpcMessage, RenderFrameDefaultsFromSparseJson)
{
	// A message carrying only the type tag must still parse, with every field
	// falling back to its documented default.
	QJsonObject obj;
	obj[QStringLiteral("type")] =
		QLatin1String(olive::ipc::msgtype::k_render_frame);

	olive::ipc::RenderFrameMsg back;
	ASSERT_TRUE(olive::ipc::RenderFrameMsg::from_json(obj, &back));
	EXPECT_EQ(back.ticket_id, 0);
	EXPECT_TRUE(back.node_uuid.isEmpty());
	EXPECT_EQ(back.time_num, 0);
	EXPECT_EQ(back.time_den, 1);
	EXPECT_EQ(back.width, 0);
	EXPECT_EQ(back.height, 0);
	EXPECT_EQ(back.format, -1);
	EXPECT_EQ(back.channel_count, 0);
	EXPECT_EQ(back.mode, 0);
	EXPECT_EQ(back.input_slot, -1);
	EXPECT_TRUE(back.input_slots.isEmpty());
	EXPECT_FALSE(back.has_color_transform);
}

TEST(IpcMessage, LargeIdentifiersSurviveRoundTrip)
{
	// 64-bit ids travel as JSON doubles, exact up to 2^53; ticket ids are
	// pointer-derived and slot sizes are byte counts, both well inside that.
	const qint64 ticket = (qint64(1) << 52) + 12345;
	const qint64 slot_bytes = qint64(7680) * 4320 * 4 * 4; // 8K RGBA float

	olive::ipc::RenderFrameMsg rf;
	rf.ticket_id = ticket;
	rf.time_num = qint64(48000) * 123456789;
	rf.time_den = qint64(1) << 40;
	olive::ipc::RenderFrameMsg rf_back;
	ASSERT_TRUE(olive::ipc::RenderFrameMsg::from_json(rf.to_json(), &rf_back));
	EXPECT_EQ(rf_back.ticket_id, ticket);
	EXPECT_EQ(rf_back.time_num, rf.time_num);
	EXPECT_EQ(rf_back.time_den, rf.time_den);

	olive::ipc::FrameReadyMsg fr;
	fr.ticket_id = ticket;
	olive::ipc::FrameReadyMsg fr_back;
	ASSERT_TRUE(olive::ipc::FrameReadyMsg::from_json(fr.to_json(), &fr_back));
	EXPECT_EQ(fr_back.ticket_id, ticket);

	olive::ipc::CancelMsg cancel;
	cancel.ticket_id = ticket;
	olive::ipc::CancelMsg cancel_back;
	ASSERT_TRUE(olive::ipc::CancelMsg::from_json(cancel.to_json(), &cancel_back));
	EXPECT_EQ(cancel_back.ticket_id, ticket);

	olive::ipc::HandshakeMsg hs;
	hs.slot_data_bytes = slot_bytes;
	hs.input_slot_data_bytes = slot_bytes / 2;
	olive::ipc::HandshakeMsg hs_back;
	ASSERT_TRUE(olive::ipc::HandshakeMsg::from_json(hs.to_json(), &hs_back));
	EXPECT_EQ(hs_back.slot_data_bytes, slot_bytes);
	EXPECT_EQ(hs_back.input_slot_data_bytes, slot_bytes / 2);
}

TEST(IpcMessage, TypedBuildersRejectMismatchedType)
{
	const QJsonObject hs_obj = olive::ipc::HandshakeMsg().to_json();
	const QJsonObject rf_obj = olive::ipc::RenderFrameMsg().to_json();
	const QJsonObject fr_obj = olive::ipc::FrameReadyMsg().to_json();
	const QJsonObject cancel_obj = olive::ipc::CancelMsg().to_json();
	const QJsonObject load_obj = olive::ipc::LoadGraphMsg().to_json();

	olive::ipc::HandshakeMsg hs_out;
	EXPECT_FALSE(olive::ipc::HandshakeMsg::from_json(rf_obj, &hs_out));
	olive::ipc::RenderFrameMsg rf_out;
	EXPECT_FALSE(olive::ipc::RenderFrameMsg::from_json(cancel_obj, &rf_out));
	olive::ipc::FrameReadyMsg fr_out;
	EXPECT_FALSE(olive::ipc::FrameReadyMsg::from_json(load_obj, &fr_out));
	olive::ipc::CancelMsg cancel_out;
	EXPECT_FALSE(olive::ipc::CancelMsg::from_json(fr_obj, &cancel_out));
	olive::ipc::LoadGraphMsg load_out;
	EXPECT_FALSE(olive::ipc::LoadGraphMsg::from_json(hs_obj, &load_out));

	// An object with no "type" at all is rejected by every parser.
	const QJsonObject empty;
	EXPECT_FALSE(olive::ipc::HandshakeMsg::from_json(empty, &hs_out));
	EXPECT_FALSE(olive::ipc::RenderFrameMsg::from_json(empty, &rf_out));
	EXPECT_FALSE(olive::ipc::FrameReadyMsg::from_json(empty, &fr_out));
	EXPECT_FALSE(olive::ipc::CancelMsg::from_json(empty, &cancel_out));
	EXPECT_FALSE(olive::ipc::LoadGraphMsg::from_json(empty, &load_out));
}

TEST(IpcMessage, ReadMessageSkipsBlankLines)
{
	olive::ipc::CancelMsg cancel;
	cancel.ticket_id = 9;
	const QByteArray line =
		QJsonDocument(cancel.to_json()).toJson(QJsonDocument::Compact);

	// A reader loop sees: blank line, whitespace-only line, then a real
	// message. Blank lines are skipped silently.
	QByteArray reader = QByteArray("\n   \n") + line + '\n';

	QJsonObject obj;
	bool ok = true;
	ASSERT_TRUE(olive::ipc::read_message(&reader, &obj, &ok));
	EXPECT_TRUE(ok);
	olive::ipc::CancelMsg back;
	ASSERT_TRUE(olive::ipc::CancelMsg::from_json(obj, &back));
	EXPECT_EQ(back.ticket_id, 9);
	EXPECT_TRUE(reader.isEmpty());
}

TEST(IpcMessage, ReadMessageRejectsNonObjectJson)
{
	// Valid JSON, but an array rather than an object: consumed, flagged not-ok.
	QByteArray reader = QByteArray("[1,2,3]\n");
	QJsonObject obj;
	bool ok = true;
	EXPECT_FALSE(olive::ipc::read_message(&reader, &obj, &ok));
	EXPECT_FALSE(ok);
	EXPECT_TRUE(reader.isEmpty());
}

TEST(IpcMessage, ReadMessageWorksWithoutOkPointer)
{
	olive::ipc::CancelMsg cancel;
	cancel.ticket_id = 4;
	QByteArray reader =
		QJsonDocument(cancel.to_json()).toJson(QJsonDocument::Compact);
	reader.append('\n');

	QJsonObject obj;
	EXPECT_TRUE(olive::ipc::read_message(&reader, &obj)); // ok defaults to nullptr

	QByteArray bad = QByteArray("garbage\n");
	EXPECT_FALSE(olive::ipc::read_message(&bad, &obj));
}

TEST(IpcMessage, WriteMessageProducesSingleTerminatedLine)
{
	QByteArray storage;
	QBuffer device(&storage);
	ASSERT_TRUE(device.open(QIODevice::WriteOnly));

	olive::ipc::HandshakeMsg hs;
	hs.protocol_version = 1;
	hs.shm_key = QStringLiteral("olive-rw-1-0");
	ASSERT_TRUE(olive::ipc::write_message(&device, hs.to_json()));
	device.close();

	// NDJSON: exactly one compact line, newline-terminated.
	EXPECT_TRUE(storage.startsWith('{'));
	EXPECT_TRUE(storage.endsWith('\n'));
	EXPECT_EQ(storage.count('\n'), 1);

	// And it parses back to an identical object.
	QJsonObject obj;
	bool ok = false;
	ASSERT_TRUE(olive::ipc::read_message(&storage, &obj, &ok));
	EXPECT_TRUE(ok);
	EXPECT_EQ(obj, hs.to_json());
}

TEST(IpcMessage, WriteMessageFailsOnClosedDevice)
{
	QByteArray storage;
	QBuffer device(&storage); // never opened: writes fail

	olive::ipc::CancelMsg cancel;
	EXPECT_FALSE(olive::ipc::write_message(&device, cancel.to_json()));
	EXPECT_TRUE(storage.isEmpty());
}

TEST(IpcMessage, MessageTypeConstantsAreDistinct)
{
	const QSet<QString> types = {
		QString::fromUtf8(olive::ipc::msgtype::k_handshake),
		QString::fromUtf8(olive::ipc::msgtype::k_load_graph),
		QString::fromUtf8(olive::ipc::msgtype::k_render_frame),
		QString::fromUtf8(olive::ipc::msgtype::k_frame_ready),
		QString::fromUtf8(olive::ipc::msgtype::k_cancel),
		QString::fromUtf8(olive::ipc::msgtype::k_graph_update),
		QString::fromUtf8(olive::ipc::msgtype::k_shutdown),
		QString::fromUtf8(olive::ipc::msgtype::k_error),
	};
	EXPECT_EQ(types.size(), 8);

	// Each builder stamps its own constant into the "type" field.
	EXPECT_EQ(olive::ipc::HandshakeMsg()
				  .to_json()
				  .value(QStringLiteral("type"))
				  .toString(),
			  QLatin1String(olive::ipc::msgtype::k_handshake));
	EXPECT_EQ(olive::ipc::RenderFrameMsg()
				  .to_json()
				  .value(QStringLiteral("type"))
				  .toString(),
			  QLatin1String(olive::ipc::msgtype::k_render_frame));
	EXPECT_EQ(olive::ipc::FrameReadyMsg()
				  .to_json()
				  .value(QStringLiteral("type"))
				  .toString(),
			  QLatin1String(olive::ipc::msgtype::k_frame_ready));
	EXPECT_EQ(olive::ipc::CancelMsg()
				  .to_json()
				  .value(QStringLiteral("type"))
				  .toString(),
			  QLatin1String(olive::ipc::msgtype::k_cancel));
	EXPECT_EQ(olive::ipc::LoadGraphMsg()
				  .to_json()
				  .value(QStringLiteral("type"))
				  .toString(),
			  QLatin1String(olive::ipc::msgtype::k_load_graph));
}

// ============================================================================
// RenderWorkerPool (validation paths that never reach a worker process)
// ============================================================================

TEST(RenderWorkerPool, RemoveTicketRejectsNull)
{
	olive::DecoderCache cache;
	olive::RenderWorkerPool pool(&cache, QStringLiteral("cpu"));

	EXPECT_FALSE(pool.remove_ticket(nullptr));
}

TEST(RenderWorkerPool, RemoveTicketUnknownTicketReturnsFalse)
{
	olive::DecoderCache cache;
	olive::RenderWorkerPool pool(&cache, QStringLiteral("cpu"));

	// The pool thread was never started, so the ticket can be neither queued
	// nor active.
	const olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
	EXPECT_FALSE(pool.remove_ticket(ticket));
}

TEST(RenderWorkerPool, ShutdownWithoutStartIsSafeAndIdempotent)
{
	olive::DecoderCache cache;
	olive::RenderWorkerPool pool(&cache, QStringLiteral("cpu"));

	// Shutdown on a pool whose thread never ran must not block or crash; the
	// destructor runs it once more when the pool goes out of scope.
	pool.shutdown();
	pool.shutdown();
	EXPECT_FALSE(pool.isRunning());
}

TEST(RenderWorkerPool, SubmitFrameRejectsNullNode)
{
	olive::DecoderCache cache;
	olive::RenderWorkerPool pool(&cache, QStringLiteral("cpu"));

	const olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
	EXPECT_FALSE(pool.submit_frame(
		ticket,
		make_video_params(nullptr, olive::VideoParams(
									 64, 64, olive::core::PixelFormat::u8, 4))));

	// A rejected submission must leave the ticket untouched.
	EXPECT_FALSE(ticket->is_running());
	EXPECT_EQ(ticket->get_finish_count(), 0);
}

TEST(RenderWorkerPool, SubmitFrameRejectsInvalidVideoParams)
{
	olive::DecoderCache cache;
	olive::RenderWorkerPool pool(&cache, QStringLiteral("cpu"));

	// A real node, but a default (zero-sized) VideoParams fails validation
	// before any project resolution or decode work happens.
	olive::Track track;
	ASSERT_FALSE(olive::VideoParams().is_valid());

	const olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
	EXPECT_FALSE(
		pool.submit_frame(ticket, make_video_params(&track, olive::VideoParams())));
	EXPECT_FALSE(ticket->is_running());
}

TEST(RenderWorkerPool, SubmitFrameRejectsNonFrameReturnType)
{
	olive::DecoderCache cache;
	olive::RenderWorkerPool pool(&cache, QStringLiteral("cpu"));

	olive::Track track;
	olive::RenderManager::RenderVideoParams params = make_video_params(
		&track, olive::VideoParams(64, 64, olive::core::PixelFormat::u8, 4));
	params.return_type = olive::RenderManager::k_texture;

	const olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
	EXPECT_FALSE(pool.submit_frame(ticket, params));
	EXPECT_FALSE(ticket->is_running());
}

// ============================================================================
// DecoderCache
// ============================================================================

TEST(DecoderCache, DefaultPairAndInsertRoundTrip)
{
	olive::DecoderCache cache;
	const olive::Decoder::CodecStream stream(
		QStringLiteral("/nonexistent/source.mov"), 2, nullptr);

	// Missing entries yield a default DecoderPair.
	const olive::DecoderPair missing = cache.value(stream);
	EXPECT_EQ(missing.decoder, nullptr);
	EXPECT_EQ(missing.last_modified, 0);

	olive::DecoderPair pair;
	pair.last_modified = qint64(1700000000123);
	cache.insert(stream, pair);

	const olive::DecoderPair fetched = cache.value(stream);
	EXPECT_EQ(fetched.decoder, nullptr);
	EXPECT_EQ(fetched.last_modified, qint64(1700000000123));

	// The cache exposes its mutex for locked access (used by the worker pool).
	EXPECT_NE(cache.mutex(), nullptr);

	// A different stream is an independent entry.
	const olive::Decoder::CodecStream other(
		QStringLiteral("/nonexistent/other.mov"), 2, nullptr);
	EXPECT_EQ(cache.value(other).last_modified, 0);
}
