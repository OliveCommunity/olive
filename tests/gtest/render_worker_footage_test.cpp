/*
 * Oak Video Editor - Render Worker Footage Integration Test
 * Copyright (C) 2026 Oak Team
 *
 * End-to-end test that spawns oak-render-worker, feeds it a real decoded
 * frame from tests/demo.mp4 through the IPC shared-memory frame pool, and
 * verifies that the worker returns a non-black output frame.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include <memory>

#include "codec/decoder.h"
#include "codec/frame.h"
#include "common/filefunctions.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/serializer/serializer.h"
#include "render/diskmanager.h"
#include "render/ipc/frameslotpool.h"
#include "render/ipc/ipcmessage.h"
#include "render/ipc/sharedmemoryregion.h"
#include "render/videoparams.h"

#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#include "render/backend/renderbackend_c.h"
#endif

using namespace olive;
using namespace olive::core;

namespace
{

#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
bool is_render_backend_available(const QString &backend)
{
	olive::DynamicRenderer renderer(backend);
	if (!renderer.load()) {
		return false;
	}

	OakRenderBackendInfo info = {};
	if (!renderer.get_backend_info(&info)) {
		return false;
	}

	if (backend == QStringLiteral("vulkan") &&
		info.kind != oak_render_backend_vulkan) {
		return false;
	}

	if (backend == QStringLiteral("opengl") &&
		info.kind != oak_render_backend_opengl) {
		return false;
	}

	return renderer.init();
}
#else
bool IsRenderBackendAvailable(const QString &)
{
	// When the dynamic backend is not built, the test binary is linked
	// directly against the renderer and we have no way to probe it cheaply
	// from here. These tests require a working GPU backend, so skip them
	// unless we can verify availability through the dynamic adapter.
	return false;
}
#endif

constexpr int k_input_slots = 1;
constexpr int k_output_slots = 1;
constexpr int k_timeout_ms = 30000;

QString worker_binary_path()
{
	// The test binary lives in cmake-build-debug/tests/gtest; the worker is in
	// cmake-build-debug/app.
	QDir dir(QCoreApplication::applicationDirPath());
	dir.cdUp(); // tests/gtest -> tests
	dir.cdUp(); // tests -> build dir
	dir.cd(QStringLiteral("app"));
#if defined(_WIN32)
	return dir.filePath(QStringLiteral("oak-render-worker.exe"));
#else
	return dir.filePath(QStringLiteral("oak-render-worker"));
#endif
}

QString demo_video_path()
{
	return QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
		.filePath(QStringLiteral("tests/demo.mp4"));
}

double sample_brightness_f32(const void *data, int width, int height, int stride)
{
	const auto *base = reinterpret_cast<const uint8_t *>(data);
	double avg = 0.0;
	int samples = 0;
	for (int y = 0; y < height && y < 1080; y += 120) {
		for (int x = 0; x < width && x < 1920; x += 240) {
			const auto *p = reinterpret_cast<const float *>(
				base + y * stride + x * 4 * sizeof(float));
			for (int c = 0; c < 3; ++c) {
				avg += p[c];
			}
			samples += 3;
		}
	}
	return samples > 0 ? avg / samples : 0.0;
}

} // namespace

class RenderWorkerFootageTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ProjectSerializer::initialize();
		DiskManager::create_instance();

		demo_path_ = demo_video_path();
		ASSERT_TRUE(QFileInfo::exists(demo_path_))
			<< "demo.mp4 not found at " << demo_path_.toStdString();

		worker_path_ = worker_binary_path();
		ASSERT_TRUE(QFileInfo::exists(worker_path_))
			<< "worker binary not found at " << worker_path_.toStdString();

		ASSERT_TRUE(temp_dir_.isValid());

		// Create a minimal project containing the demo footage.
		create_project_file();
	}

	void TearDown() override
	{
		input_region_.close();
		output_region_.close();
		if (worker_.state() != QProcess::NotRunning) {
			worker_.terminate();
			worker_.waitForFinished(5000);
			if (worker_.state() != QProcess::NotRunning) {
				worker_.kill();
				worker_.waitForFinished(5000);
			}
		}
		DiskManager::destroy_instance();
		ProjectSerializer::destroy();
	}

	void create_project_file()
	{
		project_ = std::make_unique<Project>();
		project_->initialize();

		footage_ = new Footage(demo_path_);
		footage_->setParent(project_.get());
		footage_->set_label(QStringLiteral("demo"));
		ASSERT_TRUE(footage_->is_valid())
			<< "Footage failed to probe " << demo_path_.toStdString();

		footage_id_ = QString::number(reinterpret_cast<quintptr>(footage_));

		project_file_ = FileFunctions::get_safe_temporary_filename(
			temp_dir_.filePath(QStringLiteral("worker_graph.ove")));

		ProjectSerializer::Result r = ProjectSerializer::save(
			ProjectSerializer::SaveData(ProjectSerializer::k_project,
										project_.get(), project_file_),
			false);
		ASSERT_EQ(r.code(), ProjectSerializer::k_success)
			<< "Failed to save project file: " << r.get_details().toStdString();
		ASSERT_TRUE(QFileInfo::exists(project_file_));
	}

	bool start_worker(const QString &backend)
	{
		// ---- decode a frame so we know the dimensions and slot sizes ----
		DecoderPtr decoder = Decoder::create_from_id(QStringLiteral("ffmpeg"));
		if (!decoder ||
			!decoder->open(Decoder::CodecStream(demo_path_, 0, nullptr))) {
			return false;
		}
		Decoder::RetrieveVideoParams retrieve;
		retrieve.time = Rational(0);
		retrieve.maximum_format = PixelFormat::u16;
		FramePtr frame = decoder->retrieve_video_frame(retrieve);
		if (!frame || !frame->is_allocated()) {
			return false;
		}

		input_width_ = frame->width();
		input_height_ = frame->height();
		input_stride_ = frame->linesize_bytes();
		input_bpc_ = VideoParams::get_bytes_per_channel(frame->format());
		input_data_bytes_ = frame->allocated_size();
		decoded_frame_ = frame;

		// Output at 1920x1080 float RGBA, like the real viewer path.
		output_width_ = 1920;
		output_height_ = 1080;
		output_data_bytes_ = size_t(output_width_) * output_height_ * 4 *
							 VideoParams::get_bytes_per_channel(PixelFormat::f32);

		// ---- create shared memory pools ----
		const qint64 owner_pid = QCoreApplication::applicationPid();
		output_shm_key_ = ipc::SharedMemoryRegion::make_key(owner_pid, 0);
		input_shm_key_ = ipc::SharedMemoryRegion::make_key(owner_pid, 1);

		const size_t output_bytes =
			ipc::FrameSlotPool::bytes_needed(k_output_slots, output_data_bytes_);
		const size_t input_bytes =
			ipc::FrameSlotPool::bytes_needed(k_input_slots, input_data_bytes_);

		if (!output_region_.open(output_shm_key_, output_bytes,
								 ipc::SharedMemoryRegion::k_create)) {
			return false;
		}
		if (!input_region_.open(input_shm_key_, input_bytes,
								ipc::SharedMemoryRegion::k_create)) {
			return false;
		}

		output_pool_ = std::make_unique<ipc::FrameSlotPool>(
			ipc::FrameSlotPool::create(output_region_.data(), k_output_slots,
									   output_data_bytes_));
		input_pool_ = std::make_unique<ipc::FrameSlotPool>(
			ipc::FrameSlotPool::create(input_region_.data(), k_input_slots,
									   input_data_bytes_));

		if (!output_pool_->is_valid() || !input_pool_->is_valid()) {
			return false;
		}

		// ---- spawn worker ----
		worker_.setProcessChannelMode(QProcess::SeparateChannels);
		worker_.start(worker_path_,
					  QStringList{ QStringLiteral("--backend"), backend });
		if (!worker_.waitForStarted(k_timeout_ms)) {
			return false;
		}

		// ---- wait for worker handshake ----
		if (!wait_for_message(&worker_handshake_)) {
			return false;
		}
		if (worker_handshake_[QStringLiteral("type")].toString() !=
			QLatin1String(ipc::msgtype::k_handshake)) {
			return false;
		}

		// ---- respond with our shm keys ----
		ipc::HandshakeMsg response;
		response.protocol_version = 1;
		response.shm_key = output_shm_key_;
		response.input_shm_key = input_shm_key_;
		response.input_slots = k_input_slots;
		response.output_slots = k_output_slots;
		response.slot_data_bytes = qint64(output_data_bytes_);
		response.input_slot_data_bytes = qint64(input_data_bytes_);
		if (!ipc::write_message(&worker_, response.to_json())) {
			return false;
		}

		// ---- load graph ----
		ipc::LoadGraphMsg load;
		load.path = project_file_;
		if (!ipc::write_message(&worker_, load.to_json())) {
			return false;
		}

		// ---- wait for graph_loaded ----
		QJsonObject loaded;
		if (!wait_for_message(&loaded)) {
			return false;
		}
		if (loaded[QStringLiteral("type")].toString() !=
			QLatin1String("graph_loaded")) {
			return false;
		}

		return true;
	}

	bool render_frame_and_wait(int *output_slot)
	{
		// Publish the decoded frame to the input pool. The worker consumes it and
		// releases it back, so we re-publish before every render.
		uint32_t input_slot = 0;
		if (!input_pool_->acquire(&input_slot)) {
			return false;
		}
		std::memcpy(input_pool_->slot_data(input_slot),
					decoded_frame_->const_data(), input_data_bytes_);
		ipc::FrameSlotMeta *meta = input_pool_->meta(input_slot);
		meta->id = 0;
		meta->time_num = 0;
		meta->time_den = 1;
		meta->width = input_width_;
		meta->height = input_height_;
		meta->format = int32_t(decoded_frame_->format());
		meta->channel_count = decoded_frame_->channel_count();
		meta->linesize = input_stride_;
		meta->data_size = int32_t(input_data_bytes_);
		std::strncpy(
			meta->colorspace,
			decoded_frame_->video_params().colorspace().toUtf8().constData(),
			sizeof(meta->colorspace) - 1);
		meta->colorspace[sizeof(meta->colorspace) - 1] = '\0';
		input_pool_->publish(input_slot);

		ipc::RenderFrameMsg req;
		req.ticket_id = 1;
		req.node_uuid = footage_id_;
		req.time_num = 0;
		req.time_den = 1;
		req.width = output_width_;
		req.height = output_height_;
		req.format = int(PixelFormat::f32);
		req.channel_count = VideoParams::k_rgba_channel_count;
		req.mode = int(RenderMode::k_online);
		req.input_slot = 0;
		if (!ipc::write_message(&worker_, req.to_json())) {
			std::cerr << "RenderFrameAndWait: failed to write request"
					  << std::endl;
			return false;
		}

		QJsonObject ready;
		if (!wait_for_message(&ready)) {
			std::cerr << "RenderFrameAndWait: failed to receive ready message"
					  << std::endl;
			return false;
		}
		if (ready[QStringLiteral("type")].toString() !=
			QLatin1String(ipc::msgtype::k_frame_ready)) {
			std::cerr << "RenderFrameAndWait: unexpected message type "
					  << ready[QStringLiteral("type")].toString().toStdString()
					  << " body="
					  << QJsonDocument(ready)
							 .toJson(QJsonDocument::Compact)
							 .toStdString()
					  << std::endl;
			return false;
		}
		*output_slot = ready[QStringLiteral("slot")].toInt();
		return true;
	}

	bool wait_for_message(QJsonObject *out)
	{
		QElapsedTimer timer;
		timer.start();
		while (!timer.hasExpired(k_timeout_ms)) {
			if (worker_.waitForReadyRead(100)) {
				read_buffer_.append(worker_.readAllStandardOutput());
			}
			bool ok = true;
			if (ipc::read_message(&read_buffer_, out, &ok)) {
				return true;
			}
			if (!ok) {
				std::cerr << "WaitForMessage: parse error, buffer="
						  << read_buffer_.toStdString() << std::endl;
				return false;
			}
			if (worker_.state() == QProcess::NotRunning) {
				std::cerr << "WaitForMessage: worker exited with code "
						  << worker_.exitCode() << std::endl;
				std::cerr
					<< "Worker stdout buffer: " << read_buffer_.toStdString()
					<< std::endl;
				QByteArray err = worker_.readAllStandardError();
				if (!err.isEmpty()) {
					std::cerr << "Worker stderr:\n"
							  << err.toStdString() << std::endl;
				}
				return false;
			}
		}
		std::cerr
			<< "WaitForMessage: timeout, buffer=" << read_buffer_.toStdString()
			<< std::endl;
		return false;
	}

	QString demo_path_;
	QString worker_path_;
	QString project_file_;
	QString footage_id_;
	QString output_shm_key_;
	QString input_shm_key_;
	QTemporaryDir temp_dir_;

	std::unique_ptr<Project> project_;
	Footage *footage_ = nullptr;

	QProcess worker_;
	QJsonObject worker_handshake_;
	QByteArray read_buffer_;

	ipc::SharedMemoryRegion output_region_;
	ipc::SharedMemoryRegion input_region_;
	std::unique_ptr<ipc::FrameSlotPool> output_pool_;
	std::unique_ptr<ipc::FrameSlotPool> input_pool_;

	FramePtr decoded_frame_;

	int input_width_ = 0;
	int input_height_ = 0;
	int input_stride_ = 0;
	int input_bpc_ = 0;
	size_t input_data_bytes_ = 0;

	int output_width_ = 0;
	int output_height_ = 0;
	size_t output_data_bytes_ = 0;
};

TEST_F(RenderWorkerFootageTest, VulkanFootageIsNotBlack)
{
	if (!is_render_backend_available(QStringLiteral("vulkan"))) {
		GTEST_SKIP() << "Vulkan backend is not available in this environment";
	}

	ASSERT_TRUE(start_worker(QStringLiteral("vulkan")));

	int output_slot = -1;
	ASSERT_TRUE(render_frame_and_wait(&output_slot));
	ASSERT_GE(output_slot, 0);
	ASSERT_LT(output_slot, k_output_slots);

	uint32_t consumed_slot = 0;
	ASSERT_TRUE(output_pool_->consume(&consumed_slot));
	ASSERT_EQ(int(consumed_slot), output_slot);

	const void *output_data = output_pool_->slot_data(consumed_slot);
	const double brightness =
		sample_brightness_f32(output_data, output_width_, output_height_,
							output_width_ * 4 * int(sizeof(float)));

	EXPECT_GT(brightness, 0.01)
		<< "Worker output frame is black (brightness=" << brightness << ")";

	output_pool_->release(consumed_slot);
}

TEST_F(RenderWorkerFootageTest, OpenGLFootageIsNotBlack)
{
	if (!is_render_backend_available(QStringLiteral("opengl"))) {
		GTEST_SKIP() << "OpenGL backend is not available in this environment";
	}

	ASSERT_TRUE(start_worker(QStringLiteral("opengl")));

	int output_slot = -1;
	ASSERT_TRUE(render_frame_and_wait(&output_slot));
	ASSERT_GE(output_slot, 0);
	ASSERT_LT(output_slot, k_output_slots);

	uint32_t consumed_slot = 0;
	ASSERT_TRUE(output_pool_->consume(&consumed_slot));
	ASSERT_EQ(int(consumed_slot), output_slot);

	const void *output_data = output_pool_->slot_data(consumed_slot);
	const double brightness =
		sample_brightness_f32(output_data, output_width_, output_height_,
							output_width_ * 4 * int(sizeof(float)));

	EXPECT_GT(brightness, 0.01)
		<< "Worker output frame is black (brightness=" << brightness << ")";

	output_pool_->release(consumed_slot);
}
