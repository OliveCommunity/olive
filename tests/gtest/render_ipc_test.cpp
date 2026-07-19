/*
 * Oak Video Editor - Render IPC Primitive Tests
 * Copyright (C) 2026 Oak Team
 *
 * Unit tests for the lock-free cross-process render IPC primitives:
 * - SpscRingBuffer  (single-producer/single-consumer lock-free index queue)
 * - FrameSlotPool   (shared-memory frame slot hand-off via two SPSC rings)
 * - NDJSON control message encode/decode and framing
 *
 * The threaded tests stress the lock-free invariants (no loss, no duplication, FIFO order) and are
 * intended to be run under ThreadSanitizer in CI as well.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>

#include "oakengine/spscringbuffer.h"
#include "render/ipc/frameslotpool.h"
#include "render/ipc/ipcmessage.h"

using namespace olive::ipc;

// ============================================================================
// SpscRingBuffer
// ============================================================================

TEST(SpscRingBuffer, BasicPushPopAndCapacity)
{
	std::vector<uint8_t> mem(SpscRingBuffer::bytes_needed(4));
	SpscRingBuffer *ring = SpscRingBuffer::create(mem.data(), 4);

	EXPECT_TRUE(ring->is_empty_approx());

	uint32_t v = 0;
	EXPECT_FALSE(ring->pop(&v)); // empty

	// Capacity 4 holds at most 3 entries (one slot reserved to disambiguate full/empty).
	EXPECT_TRUE(ring->push(10));
	EXPECT_TRUE(ring->push(20));
	EXPECT_TRUE(ring->push(30));
	EXPECT_FALSE(ring->push(40)); // full

	EXPECT_TRUE(ring->pop(&v));
	EXPECT_EQ(v, 10u);
	EXPECT_TRUE(ring->pop(&v));
	EXPECT_EQ(v, 20u);
	EXPECT_TRUE(ring->pop(&v));
	EXPECT_EQ(v, 30u);
	EXPECT_FALSE(ring->pop(&v)); // empty again
}

TEST(SpscRingBuffer, WrapAround)
{
	std::vector<uint8_t> mem(SpscRingBuffer::bytes_needed(4));
	SpscRingBuffer *ring = SpscRingBuffer::create(mem.data(), 4);

	// Repeatedly pushing then popping single values forces the cursors past the backing array end.
	for (uint32_t i = 0; i < 100; i++) {
		ASSERT_TRUE(ring->push(i));
		uint32_t got = 0;
		ASSERT_TRUE(ring->pop(&got));
		EXPECT_EQ(got, i);
	}
	EXPECT_TRUE(ring->is_empty_approx());
}

TEST(SpscRingBuffer, ConcurrentProducerConsumer)
{
	constexpr uint32_t k_capacity = 1024;
	constexpr uint32_t k_count =
		2'000'000; // values 0..kCount-1 streamed through the ring

	std::vector<uint8_t> mem(SpscRingBuffer::bytes_needed(k_capacity));
	SpscRingBuffer *ring = SpscRingBuffer::create(mem.data(), k_capacity);

	std::atomic<bool> order_ok{ true };

	std::thread producer([&] {
		for (uint32_t i = 0; i < k_count; i++) {
			while (!ring->push(i)) {
				std::this_thread::yield(); // buffer full, spin until consumer drains
			}
		}
	});

	std::thread consumer([&] {
		// Every value must arrive exactly once and strictly in order (FIFO).
		uint32_t expected = 0;
		while (expected < k_count) {
			uint32_t got = 0;
			if (ring->pop(&got)) {
				if (got != expected) {
					order_ok.store(false);
					return;
				}
				expected++;
			} else {
				std::this_thread::yield();
			}
		}
	});

	producer.join();
	consumer.join();

	EXPECT_TRUE(order_ok.load());
	EXPECT_TRUE(ring->is_empty_approx());
}

// ============================================================================
// FrameSlotPool
// ============================================================================

TEST(FrameSlotPool, SingleThreadedHandoff)
{
	constexpr uint32_t k_slots = 3;
	constexpr size_t k_slot_bytes = 256;

	std::vector<uint8_t> mem(FrameSlotPool::bytes_needed(k_slots, k_slot_bytes));
	FrameSlotPool filler =
		FrameSlotPool::create(mem.data(), k_slots, k_slot_bytes);
	FrameSlotPool drainer = FrameSlotPool::attach(mem.data());

	ASSERT_TRUE(filler.is_valid());
	ASSERT_TRUE(drainer.is_valid());
	EXPECT_EQ(drainer.slot_count(), k_slots);
	EXPECT_EQ(drainer.slot_data_bytes(), k_slot_bytes);

	// Fill one slot with a recognizable pattern + metadata, publish, then drain and verify.
	uint32_t idx = 0;
	ASSERT_TRUE(filler.acquire(&idx));

	auto *data = static_cast<uint8_t *>(filler.slot_data(idx));
	for (size_t i = 0; i < k_slot_bytes; i++) {
		data[i] = uint8_t(i & 0xFF);
	}
	FrameSlotMeta *meta = filler.meta(idx);
	meta->id = 4242;
	meta->width = 16;
	meta->height = 8;
	meta->data_size = int32_t(k_slot_bytes);

	ASSERT_TRUE(filler.publish(idx));

	uint32_t got_idx = 0;
	ASSERT_TRUE(drainer.consume(&got_idx));
	EXPECT_EQ(got_idx, idx);

	const FrameSlotMeta *got_meta = drainer.meta(got_idx);
	EXPECT_EQ(got_meta->id, 4242);
	EXPECT_EQ(got_meta->width, 16);

	const auto *got_data =
		static_cast<const uint8_t *>(drainer.slot_data(got_idx));
	for (size_t i = 0; i < k_slot_bytes; i++) {
		ASSERT_EQ(got_data[i], uint8_t(i & 0xFF));
	}

	EXPECT_TRUE(drainer.release(got_idx));
}

TEST(FrameSlotPool, ExhaustionAndRefill)
{
	constexpr uint32_t k_slots = 3;
	constexpr size_t k_slot_bytes = 64;

	std::vector<uint8_t> mem(FrameSlotPool::bytes_needed(k_slots, k_slot_bytes));
	FrameSlotPool pool = FrameSlotPool::create(mem.data(), k_slots, k_slot_bytes);

	// Acquire every slot, then confirm the pool reports empty.
	std::vector<uint32_t> held;
	for (uint32_t i = 0; i < k_slots; i++) {
		uint32_t a = 0;
		ASSERT_TRUE(pool.acquire(&a));
		held.push_back(a);
	}
	uint32_t overflow = 0;
	EXPECT_FALSE(pool.acquire(&overflow)); // pool exhausted

	// Publishing then consuming + releasing returns the slots to the free pool.
	for (uint32_t idx : held) {
		ASSERT_TRUE(pool.publish(idx));
	}
	for (uint32_t i = 0; i < k_slots; i++) {
		uint32_t c = 0;
		ASSERT_TRUE(pool.consume(&c));
		ASSERT_TRUE(pool.release(c));
	}
	uint32_t again = 0;
	EXPECT_TRUE(pool.acquire(&again)); // free again
}

TEST(FrameSlotPool, ConcurrentFillDrainIntegrity)
{
	constexpr uint32_t k_slots = 8;
	constexpr size_t k_slot_bytes = 4096;
	constexpr int64_t k_frames = 200'000;

	std::vector<uint8_t> mem(FrameSlotPool::bytes_needed(k_slots, k_slot_bytes));
	FrameSlotPool filler =
		FrameSlotPool::create(mem.data(), k_slots, k_slot_bytes);
	FrameSlotPool drainer = FrameSlotPool::attach(mem.data());

	std::atomic<bool> integrity_ok{ true };

	// Filler: for each frame id, acquire a slot, stamp the id into meta and a pattern into the data,
	// publish. Spins when no slot is free (this is the natural backpressure path).
	std::thread fill_thread([&] {
		for (int64_t id = 0; id < k_frames; id++) {
			uint32_t idx = 0;
			while (!filler.acquire(&idx)) {
				std::this_thread::yield();
			}
			filler.meta(idx)->id = id;
			auto *d = static_cast<uint8_t *>(filler.slot_data(idx));
			const uint8_t pat = uint8_t(id & 0xFF);
			memset(d, pat, k_slot_bytes);
			while (!filler.publish(idx)) {
				std::this_thread::yield(); // ready ring transiently full
			}
		}
	});

	// Drainer: consume in order, verify the id is monotonic and the data matches the id pattern,
	// then release the slot back to the filler.
	std::thread drain_thread([&] {
		int64_t expected = 0;
		while (expected < k_frames) {
			uint32_t idx = 0;
			if (!drainer.consume(&idx)) {
				std::this_thread::yield();
				continue;
			}
			const FrameSlotMeta *m = drainer.meta(idx);
			if (m->id != expected) {
				integrity_ok.store(false);
				return;
			}
			const auto *d = static_cast<const uint8_t *>(drainer.slot_data(idx));
			const uint8_t pat = uint8_t(expected & 0xFF);
			if (d[0] != pat || d[k_slot_bytes - 1] != pat) {
				integrity_ok.store(false);
				return;
			}
			while (!drainer.release(idx)) {
				std::this_thread::yield();
			}
			expected++;
		}
	});

	fill_thread.join();
	drain_thread.join();

	EXPECT_TRUE(integrity_ok.load());
}

// ============================================================================
// NDJSON control messages
// ============================================================================

TEST(IpcMessage, TypedRoundTrip)
{
	// Write several typed messages into a buffer, then drain and parse them back the way a pipe
	// reader would.
	QByteArray storage;
	QBuffer dev(&storage);
	ASSERT_TRUE(dev.open(QIODevice::WriteOnly));

	HandshakeMsg hs;
	hs.protocol_version = 1;
	hs.shm_key = QStringLiteral("olive-rw-1234-0");
	hs.input_shm_key = QStringLiteral("olive-in-1234-0");
	hs.input_slots = 4;
	hs.output_slots = 6;
	hs.slot_data_bytes = 256ll * 1024 * 1024;
	hs.input_slot_data_bytes = 128ll * 1024 * 1024;
	ASSERT_TRUE(write_message(&dev, hs.to_json()));

	RenderFrameMsg rf;
	rf.ticket_id = 99;
	rf.node_uuid = QStringLiteral("{abcd-1234}");
	rf.time_num = 1001;
	rf.time_den = 30000;
	rf.width = 1920;
	rf.height = 1080;
	rf.format = 3;
	rf.channel_count = 4;
	rf.mode = 1;
	rf.input_slot = 2;
	rf.input_slots = { 2, 3 };
	ASSERT_TRUE(write_message(&dev, rf.to_json()));

	FrameReadyMsg fr;
	fr.ticket_id = 99;
	fr.output_slot = 2;
	ASSERT_TRUE(write_message(&dev, fr.to_json()));

	dev.close();

	QByteArray reader = storage;
	QJsonObject obj;
	bool ok = false;

	ASSERT_TRUE(read_message(&reader, &obj, &ok));
	ASSERT_TRUE(ok);
	HandshakeMsg hs2;
	ASSERT_TRUE(HandshakeMsg::from_json(obj, &hs2));
	EXPECT_EQ(hs2.protocol_version, 1);
	EXPECT_EQ(hs2.shm_key, hs.shm_key);
	EXPECT_EQ(hs2.input_shm_key, hs.input_shm_key);
	EXPECT_EQ(hs2.input_slots, 4);
	EXPECT_EQ(hs2.output_slots, 6);
	EXPECT_EQ(hs2.slot_data_bytes, hs.slot_data_bytes);
	EXPECT_EQ(hs2.input_slot_data_bytes, hs.input_slot_data_bytes);

	ASSERT_TRUE(read_message(&reader, &obj, &ok));
	ASSERT_TRUE(ok);
	RenderFrameMsg rf2;
	ASSERT_TRUE(RenderFrameMsg::from_json(obj, &rf2));
	EXPECT_EQ(rf2.ticket_id, 99);
	EXPECT_EQ(rf2.node_uuid, rf.node_uuid);
	EXPECT_EQ(rf2.time_num, 1001);
	EXPECT_EQ(rf2.time_den, 30000);
	EXPECT_EQ(rf2.width, 1920);
	EXPECT_EQ(rf2.format, 3);
	EXPECT_EQ(rf2.input_slot, 2);
	ASSERT_EQ(rf2.input_slots.size(), 2);
	EXPECT_EQ(rf2.input_slots[0], 2);
	EXPECT_EQ(rf2.input_slots[1], 3);

	ASSERT_TRUE(read_message(&reader, &obj, &ok));
	ASSERT_TRUE(ok);
	FrameReadyMsg fr2;
	ASSERT_TRUE(FrameReadyMsg::from_json(obj, &fr2));
	EXPECT_EQ(fr2.ticket_id, 99);
	EXPECT_EQ(fr2.output_slot, 2);

	// No more complete lines remain.
	EXPECT_FALSE(read_message(&reader, &obj, &ok));
}

TEST(IpcMessage, PartialFrameByteByByte)
{
	CancelMsg c;
	c.ticket_id = 7;
	const QByteArray full =
		QByteArray(QJsonDocument(c.to_json()).toJson(QJsonDocument::Compact)) +
		'\n';

	// Feed the bytes one at a time; ReadMessage must return false until the terminating '\n'.
	QByteArray reader;
	QJsonObject obj;
	bool ok = false;
	for (int i = 0; i < full.size() - 1; i++) {
		reader.append(full.at(i));
		ASSERT_FALSE(read_message(&reader, &obj, &ok)); // no complete line yet
	}
	reader.append(full.at(full.size() - 1)); // the trailing newline
	ASSERT_TRUE(read_message(&reader, &obj, &ok));
	ASSERT_TRUE(ok);

	CancelMsg c2;
	ASSERT_TRUE(CancelMsg::from_json(obj, &c2));
	EXPECT_EQ(c2.ticket_id, 7);
}

TEST(IpcMessage, MalformedLineIsSkipped)
{
	QByteArray reader = QByteArray("this is not json\n");
	QJsonObject obj;
	bool ok = true;
	// A complete but malformed line is consumed and reported as not-ok, leaving the buffer drained.
	EXPECT_FALSE(read_message(&reader, &obj, &ok));
	EXPECT_FALSE(ok);
	EXPECT_TRUE(reader.isEmpty());
}

TEST(IpcMessage, BlankLinesAreSkippedSilently)
{
	CancelMsg c;
	c.ticket_id = 7;
	const QByteArray line =
		QByteArray(QJsonDocument(c.to_json()).toJson(QJsonDocument::Compact)) +
		'\n';

	// Blank lines (even repeated) are consumed without flagging an error, and
	// the following real message is still parsed.
	QByteArray reader = QByteArray("\n \n\n") + line;
	QJsonObject obj;
	bool ok = false;
	ASSERT_TRUE(read_message(&reader, &obj, &ok));
	EXPECT_TRUE(ok);

	CancelMsg c2;
	ASSERT_TRUE(CancelMsg::from_json(obj, &c2));
	EXPECT_EQ(c2.ticket_id, 7);

	// Only blank lines left: nothing more to read, but still not an error.
	ok = true;
	EXPECT_FALSE(read_message(&reader, &obj, &ok));
	EXPECT_TRUE(ok);
	EXPECT_TRUE(reader.isEmpty());
}

TEST(IpcMessage, WrongTypeRejected)
{
	// FromJson must reject an object whose "type" does not match the target struct.
	HandshakeMsg hs;
	hs.protocol_version = 1;
	const QJsonObject obj = hs.to_json();

	RenderFrameMsg rf;
	EXPECT_FALSE(RenderFrameMsg::from_json(obj, &rf));
}
