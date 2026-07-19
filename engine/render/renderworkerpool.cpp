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

#include "renderworkerpool.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QXmlStreamWriter>
#include <algorithm>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <signal.h>
#endif

#include "codec/frame.h"
#include "common/qtutils.h"
#include "node/project/footage/footage.h"
#include "node/traverser.h"
#include "oliveimpl/render/ipc/frameslotpool.h"
#include "oliveimpl/render/ipc/ipcmessage.h"
#include "oliveimpl/render/ipc/sharedmemoryregion.h"

namespace olive
{

namespace
{

constexpr int k_protocol_version = 1;

struct FootageInput {
	FootageJob job;
	Rational time;
};

class FootageInputCollector : public NodeTraverser {
public:
	QVector<FootageInput>
	collect(const RenderManager::RenderVideoParams &params, CancelAtom *cancel)
	{
		set_cancel_pointer(cancel);
		VideoParams cache_params = params.video_params;
		cache_params.set_format(PixelFormat::f32);
		set_cache_video_params(cache_params);
		set_cache_audio_params(params.audio_params);

		Rational frame_length = cache_params.frame_rate_as_time_base();
		if (cache_params.interlacing() != VideoParams::k_interlace_none) {
			frame_length /= 2;
		}
		NodeValueTable table = generate_table(
			params.node, TimeRange(params.time, params.time + frame_length));
		NodeValue texture = table.get(NodeValue::k_texture);
		resolve_jobs(texture);

		if (cache_params.interlacing() != VideoParams::k_interlace_none) {
			NodeValueTable second_table = generate_table(
				params.node, TimeRange(params.time + frame_length,
									   params.time + frame_length * 2));
			NodeValue second_texture = second_table.get(NodeValue::k_texture);
			resolve_jobs(second_texture);
		}

		return inputs_;
	}

protected:
	void process_video_footage(TexturePtr destination, const FootageJob *stream,
							 const Rational &input_time) override
	{
		Q_UNUSED(destination)
		if (stream) {
			inputs_.append({ *stream, input_time });
		}
	}

private:
	QVector<FootageInput> inputs_;
};

DecoderPtr resolve_decoder_from_cache(DecoderCache *decoder_cache,
								   const QString &decoder_id,
								   const Decoder::CodecStream &stream)
{
	if (!decoder_cache || !stream.is_valid()) {
		return nullptr;
	}

	QMutexLocker locker(decoder_cache->mutex());
	DecoderPair decoder = decoder_cache->value(stream);
	const qint64 file_last_modified =
		QFileInfo(stream.filename()).lastModified().toMSecsSinceEpoch();

	if (decoder.decoder && decoder.last_modified == file_last_modified) {
		return decoder.decoder;
	}

	decoder.decoder = Decoder::create_from_id(decoder_id);
	decoder.last_modified = file_last_modified;
	decoder_cache->insert(stream, decoder);
	locker.unlock();

	if (!decoder.decoder || !decoder.decoder->open(stream)) {
		qWarning() << "RenderWorkerPool failed to open decoder for"
				   << stream.filename() << "::" << stream.stream();
		return nullptr;
	}

	return decoder.decoder;
}

FramePtr decode_input_frame(DecoderCache *decoder_cache,
						  const FootageInput &input, CancelAtom *cancel,
						  RenderMode::Mode mode)
{
	VideoParams stream_data = input.job.video_params();
	QString filename = input.job.filename();
	QString decoder_id = input.job.decoder();
	int stream_index = stream_data.stream_index();

	// Use the generated proxy when this render is allowed to (preview only,
	// never export). The Footage node only attaches a proxy when it is
	// enabled, ready, and matches this stream.
	if (input.job.should_use_proxy(mode)) {
		filename = input.job.proxy_filename();
		decoder_id = input.job.proxy_decoder();
		stream_index = input.job.proxy_stream_index();
	}

	DecoderPtr decoder;

	switch (stream_data.video_type()) {
	case VideoParams::k_video_type_video:
	case VideoParams::k_video_type_still:
		decoder = resolve_decoder_from_cache(
			decoder_cache, decoder_id,
			Decoder::CodecStream(filename, stream_index, nullptr));
		break;
	case VideoParams::k_video_type_image_sequence: {
		const int64_t frame_number =
			stream_data.get_time_in_timebase_units(input.time);
		filename =
			Decoder::transform_image_sequence_file_name(filename, frame_number);
		decoder = Decoder::create_from_id(decoder_id);
		if (decoder && !decoder->open(Decoder::CodecStream(
						   filename, stream_index, nullptr))) {
			decoder = nullptr;
		}
		break;
	}
	}

	if (!decoder) {
		return nullptr;
	}

	Decoder::RetrieveVideoParams retrieve;
	retrieve.divider = stream_data.divider();
	retrieve.maximum_format = PixelFormat::u16;
	retrieve.time = stream_data.video_type() == VideoParams::k_video_type_video ?
						input.time :
						Decoder::k_any_timecode;
	retrieve.cancelled = cancel;
	retrieve.force_range = stream_data.color_range();
	retrieve.src_interlacing = stream_data.interlacing();
	FramePtr frame = decoder->retrieve_video_frame(retrieve);
	if (frame) {
		frame->set_timestamp(input.time);

		// Ensure the frame carries the colorspace the color manager expects.
		// Decoders do not always set this on the returned frame, but the worker
		// needs it to build the correct OCIO transform.
		VideoParams frame_params = frame->video_params();
		if (frame_params.colorspace().isEmpty() &&
			!stream_data.colorspace().isEmpty()) {
			frame_params.set_colorspace(stream_data.colorspace());
			frame->set_video_params(frame_params);
		}
	}
	return frame;
}

bool decode_input_frames(DecoderCache *decoder_cache,
					   const RenderManager::RenderVideoParams &params,
					   CancelAtom *cancel, QVector<FramePtr> *frames)
{
	frames->clear();

	FootageInputCollector collector;
	const QVector<FootageInput> inputs = collector.collect(params, cancel);
	frames->reserve(inputs.size());
	for (const FootageInput &input : inputs) {
		if (cancel && cancel->is_cancelled()) {
			return false;
		}

		FramePtr frame =
			decode_input_frame(decoder_cache, input, cancel, params.mode);
		if (!frame || !frame->is_allocated()) {
			frames->clear();
			return false;
		}
		frames->append(frame);
	}

	return true;
}

QString worker_program_path()
{
#if defined(Q_OS_WIN)
	const QString file = QStringLiteral("oak-render-worker.exe");
#else
	const QString file = QStringLiteral("oak-render-worker");
#endif

	const QString app_dir = QCoreApplication::applicationDirPath();
	const QStringList candidates = {
		QDir(app_dir).filePath(file),
		QDir(app_dir).filePath(QStringLiteral("../app/") + file),
		QDir(app_dir).filePath(QStringLiteral("../worker/") + file),
	};

	for (const QString &path : candidates) {
		if (QFileInfo::exists(path)) {
			return path;
		}
	}

	if (qEnvironmentVariableIsSet("OAK_RENDER_WORKER")) {
		return QString::fromUtf8(qgetenv("OAK_RENDER_WORKER"));
	}

	return candidates.first();
}

bool write_control_message(QProcess *process, const QJsonObject &obj)
{
	if (!process || process->state() != QProcess::Running) {
		return false;
	}

	const QByteArray line =
		QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
	const qint64 written = process->write(line);
	if (written != line.size()) {
		return false;
	}
	return process->waitForBytesWritten(5000);
}

void try_write_control_message(QProcess *process, const QJsonObject &obj)
{
	if (!process || process->state() != QProcess::Running) {
		return;
	}

	const QByteArray line =
		QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
	process->write(line);
}

bool kill_process_by_id(qint64 process_id)
{
	if (process_id <= 0) {
		return false;
	}

#if defined(Q_OS_WIN)
	HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(process_id));
	if (!handle) {
		return false;
	}
	const bool ok = TerminateProcess(handle, 1) != 0;
	CloseHandle(handle);
	return ok;
#else
	return ::kill(pid_t(process_id), SIGKILL) == 0;
#endif
}

bool is_process_alive(qint64 process_id)
{
	if (process_id <= 0) {
		return false;
	}

#if defined(Q_OS_WIN)
	HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
								DWORD(process_id));
	if (!handle) {
		return false;
	}
	DWORD exit_code = 0;
	const bool alive = GetExitCodeProcess(handle, &exit_code) &&
					   exit_code == STILL_ACTIVE;
	CloseHandle(handle);
	return alive;
#else
	return ::kill(pid_t(process_id), 0) == 0;
#endif
}

QString worker_process_details(const QProcess *process)
{
	if (!process) {
		return QStringLiteral("worker process unavailable");
	}

	const QString exit_status = process->exitStatus() == QProcess::CrashExit ?
									QStringLiteral("crash") :
									QStringLiteral("normal");
	return QStringLiteral(
			   "state=%1 exit_status=%2 exit_code=%3 process_error=%4 error=\"%5\"")
		.arg(int(process->state()))
		.arg(exit_status)
		.arg(process->exitCode())
		.arg(int(process->error()))
		.arg(process->errorString());
}

bool read_control_message(QProcess *process, QJsonObject *out, QString *error,
						int timeout_ms = 10000)
{
	if (!process->waitForReadyRead(timeout_ms)) {
		if (error) {
			if (process->state() == QProcess::NotRunning) {
				*error = QStringLiteral("worker exited before response: %1")
							 .arg(worker_process_details(process));
			} else {
				*error =
					QStringLiteral("timeout waiting for worker response: %1")
						.arg(worker_process_details(process));
			}
		}
		return false;
	}

	while (process->canReadLine()) {
		const QByteArray line = process->readLine().trimmed();
		if (line.isEmpty()) {
			continue;
		}

		QJsonParseError parse_error;
		const QJsonDocument doc = QJsonDocument::fromJson(line, &parse_error);
		if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
			if (error) {
				*error =
					QStringLiteral("worker emitted malformed control JSON");
			}
			return false;
		}

		*out = doc.object();
		if (out->value(QStringLiteral("type")).toString() ==
			QLatin1String(engine::internal::ipc::msgtype::k_error)) {
			if (error) {
				*error = out->value(QStringLiteral("message")).toString();
			}
			return false;
		}
		return true;
	}

	if (error) {
		*error = QStringLiteral("worker response did not contain a full line");
	}
	return false;
}

} // namespace

// Holds the persistent per-worker IPC state. Defined here rather than in the
// header because the member types are engine-internal (oliveimpl): the header
// is consumed outside the library and only forward-declares this struct.
struct RenderWorkerPool::PooledWorker {
	QProcess *process = nullptr;
	QString loaded_graph_path;
	qint64 last_used_ms = 0;
	int use_count = 0;

	// Persistent shared memory for this worker. Reusing regions across frames
	// avoids the cost of creating/destroying large shm segments every render.
	engine::internal::ipc::SharedMemoryRegion output_region;
	engine::internal::ipc::FrameSlotPool output_pool;
	size_t output_slot_bytes = 0;
	QString output_shm_key;

	engine::internal::ipc::SharedMemoryRegion input_region;
	engine::internal::ipc::FrameSlotPool input_pool;
	size_t input_slot_bytes = 0;
	QString input_shm_key;
};

RenderWorkerPool::RenderWorkerPool(DecoderCache *decoder_cache,
								   const QString &gpu_backend, QObject *parent)
	: QThread(parent)
	, decoder_cache_(decoder_cache)
	, gpu_backend_(gpu_backend)
{
}

RenderWorkerPool::~RenderWorkerPool()
{
	shutdown();
}

bool RenderWorkerPool::submit_frame(
	RenderTicketPtr ticket, const RenderManager::RenderVideoParams &params)
{
	Job job(ticket, params);
	if (!prepare_job(ticket, params, &job)) {
		return false;
	}

	ticket->moveToThread(this);

	QMutexLocker locker(&mutex_);
	queue_.push_back(job);
	wait_.wakeOne();
	return true;
}

bool RenderWorkerPool::remove_ticket(RenderTicketPtr ticket)
{
	if (!ticket) {
		return false;
	}

	QString queued_graph_path;
	bool matched_active = false;

	{
		QMutexLocker locker(&mutex_);
		auto it = std::find_if(queue_.begin(), queue_.end(),
							   [&ticket](const Job &job) {
								   return job.ticket == ticket;
							   });
		if (it != queue_.end()) {
			queued_graph_path = it->graph_path;
			queue_.erase(it);
		} else {
			for (const ActiveJob &active : active_jobs_) {
				if (active.ticket == ticket) {
					ticket->cancel();
					cancel_active_process(active.process_id);
					matched_active = true;
					break;
				}
			}
			if (!matched_active) {
				return false;
			}
		}
	}

	if (!queued_graph_path.isEmpty()) {
		release_graph_path_ref(queued_graph_path);
		return true;
	}

	return true;
}

void RenderWorkerPool::shutdown()
{
	{
		QMutexLocker locker(&mutex_);
		stopping_ = true;
		for (Job &job : queue_) {
			if (job.ticket) {
				job.ticket->cancel();
			}
			release_graph_path_ref_locked(job.graph_path);
		}
		queue_.clear();
		for (ActiveJob &active : active_jobs_) {
			if (active.ticket) {
				active.ticket->cancel();
				cancel_active_process(active.process_id);
			}
		}
		for (auto it = graph_cache_.begin(); it != graph_cache_.end(); ++it) {
			set_graph_path_cached_locked(it->path, false);
		}
		graph_cache_.clear();
		wait_.wakeAll();
	}

	if (isRunning()) {
		wait();
	}
}

void RenderWorkerPool::run()
{
	const int count = worker_count();
	{
		QMutexLocker locker(&mutex_);
		active_jobs_.resize(count);
	}

	std::vector<std::vector<std::unique_ptr<PooledWorker>>> local_pools(count);
	std::vector<std::thread> workers;
	workers.reserve(size_t(count));
	for (int i = 0; i < count; i++) {
		workers.emplace_back(
			[this, i, &local_pools]() { worker_loop(i, &local_pools[i]); });
	}

	for (std::thread &worker : workers) {
		worker.join();
	}

	for (auto &local_pool : local_pools) {
		shutdown_local_pool(&local_pool);
	}

	clear_graph_cache();

	QMutexLocker locker(&mutex_);
	active_jobs_.clear();
}

void RenderWorkerPool::worker_loop(
	int worker_index, std::vector<std::unique_ptr<PooledWorker>> *local_pool)
{
	while (true) {
		mutex_.lock();
		while (queue_.empty() && !stopping_) {
			wait_.wait(&mutex_);
		}
		if (stopping_ && queue_.empty()) {
			mutex_.unlock();
			break;
		}

		Job job = queue_.front();
		queue_.pop_front();
		mutex_.unlock();

		process_job(job, worker_index, local_pool);
		release_graph_path_ref(job.graph_path);
	}
}

bool RenderWorkerPool::prepare_job(RenderTicketPtr ticket,
								  const RenderManager::RenderVideoParams &params,
								  Job *job)
{
	if (!is_supported(params)) {
		return false;
	}

	Project *project = Project::get_project_from_object(params.node);
	if (!project) {
		qWarning()
			<< "RenderWorkerPool could not resolve project for render node";
		return false;
	}

	QVector<FramePtr> input_frames;
	if (!decode_input_frames(decoder_cache_, params, ticket->get_cancel_atom(),
						   &input_frames)) {
		qWarning() << "RenderWorkerPool could not predecode footage inputs;"
				   << "falling back to in-process render";
		return false;
	}

	QString graph_path;
	bool wrote_new_snapshot = false;
	{
		const QUuid project_uuid = project->get_uuid();
		QMutexLocker locker(&mutex_);
		auto it = graph_cache_.find(project_uuid);
		if (it != graph_cache_.end() && !project->is_modified()) {
			graph_path = it->path;
			add_graph_path_ref_locked(graph_path);
		} else {
			if (it != graph_cache_.end()) {
				set_graph_path_cached_locked(it->path, false);
				graph_cache_.erase(it);
			}
			locker.unlock();
			if (!write_graph_snapshot(project, &graph_path)) {
				return false;
			}
			wrote_new_snapshot = true;
			// For internal render-proxy copies, the snapshot now represents the
			// serialized state, so reset the modified flag so the snapshot can be
			// reused until the copy changes again.
			if (project->property("_oak_render_proxy").toBool()) {
				project->set_modified(false);
			}
			locker.relock();
			graph_cache_.insert(project_uuid, { graph_path });
			set_graph_path_cached_locked(graph_path, true);
			add_graph_path_ref_locked(graph_path);
		}
	}

	job->ticket = ticket;
	job->params = params;
	job->graph_path = graph_path;
	job->node_token = QString::number(reinterpret_cast<quintptr>(params.node));
	job->input_frames = input_frames;
	Q_UNUSED(wrote_new_snapshot)
	return true;
}

bool RenderWorkerPool::write_graph_snapshot(Project *project, QString *path)
{
	// Keep snapshots in the system temp directory. The previous bug was not the
	// temp location itself, but stale snapshots being deleted while queued jobs
	// still referenced them.
	const QString graph_dir = QDir::tempPath();

	QTemporaryFile file(QDir(graph_dir).filePath(
		QStringLiteral("oak-render-graph-XXXXXX.ove")));
	file.setAutoRemove(false);
	if (!file.open()) {
		qWarning()
			<< "RenderWorkerPool failed to create graph snapshot temp file"
			<< file.errorString();
		return false;
	}

	QXmlStreamWriter writer(&file);
	ProjectSerializer::SaveData data(ProjectSerializer::k_project, project,
									 file.fileName());
	const ProjectSerializer::Result result =
		ProjectSerializer::save(&writer, data);
	file.close();

	if (result.code() != ProjectSerializer::k_success || writer.hasError()) {
		qWarning() << "RenderWorkerPool failed to serialize graph snapshot"
				   << result.get_details();
		QFile::remove(file.fileName());
		return false;
	}

	*path = file.fileName();
	return true;
}

bool RenderWorkerPool::is_supported(
	const RenderManager::RenderVideoParams &params) const
{
	return params.node && params.return_type == RenderManager::k_frame &&
		   params.video_params.is_valid();
}

void RenderWorkerPool::process_job(
	const Job &job, int worker_index,
	std::vector<std::unique_ptr<PooledWorker>> *local_pool)
{
	const qint64 ticket_id =
		qint64(reinterpret_cast<quintptr>(job.ticket.get()));
	set_active_worker(worker_index, job.ticket, nullptr, ticket_id);

	job.ticket->start();
	if (job.ticket->is_cancelled()) {
		job.ticket->finish();
		clear_active_worker(worker_index, 0);
		return;
	}

	std::unique_ptr<PooledWorker> worker =
		acquire_worker(local_pool, job.graph_path);
	if (!worker) {
		qWarning() << "RenderWorkerPool failed to acquire worker for ticket"
				   << ticket_id;
		job.ticket->finish();
		clear_active_worker(worker_index, 0);
		return;
	}

	for (int attempt = 0; attempt < k_max_attempts; attempt++) {
		if (attempt > 0) {
			worker = acquire_worker(local_pool, job.graph_path);
			if (!worker) {
				qWarning()
					<< "RenderWorkerPool failed to acquire worker for retry"
					<< ticket_id;
				break;
			}
		}

		const JobResult result =
			process_job_attempt(job, worker_index, attempt, worker.get());
		const qint64 worker_pid =
			worker && worker->process ? worker->process->processId() : 0;
		const bool process_state_running = worker && worker->process &&
										   worker->process->state() ==
											   QProcess::Running;
		const bool os_alive = worker_pid > 0 && is_process_alive(worker_pid);
		const bool worker_healthy = process_state_running || os_alive;
		const bool keep_alive = (result == JobResult::k_finished) &&
								worker_healthy;

		return_worker(local_pool, std::move(worker), keep_alive);
		worker.reset();

		if (result == JobResult::k_finished) {
			clear_active_worker(worker_index, 0);
			return;
		}
		if (result == JobResult::k_cancelled) {
			job.ticket->finish();
			clear_active_worker(worker_index, 0);
			return;
		}
		if (result == JobResult::k_fatal_failure) {
			break;
		}
		if (attempt + 1 < k_max_attempts && !job.ticket->is_cancelled()) {
			qWarning() << "RenderWorkerPool retrying render worker for ticket"
					   << ticket_id << "after worker failure";
		}
	}

	if (job.ticket->is_cancelled()) {
		job.ticket->finish();
	} else {
		qWarning() << "RenderWorkerPool exhausted worker retries for ticket"
				   << ticket_id;
		job.ticket->finish();
	}
	clear_active_worker(worker_index, 0);
}

RenderWorkerPool::JobResult
RenderWorkerPool::process_job_attempt(const Job &job, int worker_index,
									int attempt_index, PooledWorker *worker)
{
	const qint64 ticket_id =
		qint64(reinterpret_cast<quintptr>(job.ticket.get()));
	if (job.ticket->is_cancelled()) {
		return JobResult::k_cancelled;
	}

	if (!worker || !worker->process) {
		return JobResult::k_retryable_failure;
	}

	const qint64 worker_process_id = worker->process->processId();

	const int output_width = job.params.force_size.width() > 0 ?
								 job.params.force_size.width() :
								 job.params.video_params.effective_width();
	const int output_height = job.params.force_size.height() > 0 ?
								  job.params.force_size.height() :
								  job.params.video_params.effective_height();
	const PixelFormat::Format output_format =
		job.params.force_format != PixelFormat::invalid ?
			PixelFormat::Format(job.params.force_format) :
			PixelFormat::f32;
	const int output_channels = job.params.force_channel_count > 0 ?
									job.params.force_channel_count :
									VideoParams::k_rgba_channel_count;
	const int output_linesize = Frame::generate_linesize_bytes(
		output_width, output_format, output_channels);
	const size_t estimated_output_slot_bytes =
		size_t(output_linesize) * size_t(output_height);
	const int f32_rgba_linesize = Frame::generate_linesize_bytes(
		output_width, PixelFormat::f32, VideoParams::k_rgba_channel_count);
	const size_t f32_rgba_slot_bytes =
		size_t(f32_rgba_linesize) * size_t(output_height);
	const size_t output_slot_bytes =
		std::max(estimated_output_slot_bytes, f32_rgba_slot_bytes);
	size_t input_slot_bytes = 0;
	for (const FramePtr &frame : job.input_frames) {
		if (frame && frame->is_allocated()) {
			input_slot_bytes =
				std::max(input_slot_bytes, size_t(frame->allocated_size()));
		}
	}
	const size_t output_region_bytes =
		engine::internal::ipc::FrameSlotPool::bytes_needed(k_output_slots, output_slot_bytes);

	if (!worker->output_region.is_valid() ||
		worker->output_slot_bytes < output_slot_bytes) {
		if (worker->output_region.is_valid()) {
			worker->output_region.close();
			worker->output_pool = engine::internal::ipc::FrameSlotPool();
		}
		if (worker->output_shm_key.isEmpty()) {
			worker->output_shm_key =
				engine::internal::ipc::SharedMemoryRegion::make_key(worker_process_id, 0) +
				QStringLiteral("-out");
		}
		if (!worker->output_region.open(worker->output_shm_key,
										output_region_bytes,
										engine::internal::ipc::SharedMemoryRegion::k_create)) {
			qWarning()
				<< "RenderWorkerPool failed to create output shared memory"
				<< worker->output_region.error();
			return JobResult::k_fatal_failure;
		}
		worker->output_pool = engine::internal::ipc::FrameSlotPool::create(
			worker->output_region.data(), k_output_slots, output_slot_bytes);
		worker->output_slot_bytes = output_slot_bytes;
	}
	const QString shm_key = worker->output_shm_key;
	engine::internal::ipc::FrameSlotPool &output_pool = worker->output_pool;

	const uint32_t input_slot_count =
		job.input_frames.isEmpty() ? 0 : uint32_t(job.input_frames.size());
	if (input_slot_count > 0) {
		if (!worker->input_region.is_valid() ||
			worker->input_slot_bytes < input_slot_bytes ||
			worker->input_pool.slot_count() < input_slot_count) {
			if (worker->input_region.is_valid()) {
				worker->input_region.close();
				worker->input_pool = engine::internal::ipc::FrameSlotPool();
			}
			if (worker->input_shm_key.isEmpty()) {
				worker->input_shm_key =
					engine::internal::ipc::SharedMemoryRegion::make_key(worker_process_id, 1) +
					QStringLiteral("-in");
			}
			const size_t input_region_bytes = engine::internal::ipc::FrameSlotPool::bytes_needed(
				input_slot_count, input_slot_bytes);
			if (!worker->input_region.open(worker->input_shm_key,
										   input_region_bytes,
										   engine::internal::ipc::SharedMemoryRegion::k_create)) {
				qWarning()
					<< "RenderWorkerPool failed to create input shared memory"
					<< worker->input_region.error();
				return JobResult::k_fatal_failure;
			}
			worker->input_pool =
				engine::internal::ipc::FrameSlotPool::create(worker->input_region.data(),
										   input_slot_count, input_slot_bytes);
			worker->input_slot_bytes = input_slot_bytes;
		}
	}
	const QString input_shm_key = worker->input_shm_key;
	engine::internal::ipc::FrameSlotPool &input_pool = worker->input_pool;
	QVector<int> input_slots;
	if (input_slot_count > 0) {
		for (const FramePtr &frame : job.input_frames) {
			if (frame->allocated_size() > int(worker->input_slot_bytes)) {
				qWarning()
					<< "RenderWorkerPool decoded input frame exceeds slot size";
				return JobResult::k_fatal_failure;
			}

			uint32_t slot = 0;
			if (!input_pool.acquire(&slot)) {
				qWarning() << "RenderWorkerPool input pool had no free slot";
				return JobResult::k_fatal_failure;
			}

			memcpy(input_pool.slot_data(slot), frame->const_data(),
				   size_t(frame->allocated_size()));
			engine::internal::ipc::FrameSlotMeta *meta = input_pool.meta(slot);
			meta->id = qint64(input_slots.size());
			meta->time_num = frame->timestamp().numerator();
			meta->time_den = frame->timestamp().denominator();
			meta->width = frame->width();
			meta->height = frame->height();
			meta->format = int32_t(frame->format());
			meta->channel_count = frame->channel_count();
			meta->linesize = frame->linesize_bytes();
			meta->data_size = frame->allocated_size();
			memset(meta->colorspace, 0, sizeof(meta->colorspace));
			const QString cs = frame->video_params().colorspace();
			if (!cs.isEmpty()) {
				const QByteArray cs_utf8 = cs.toUtf8();
				const size_t copy_len =
					qMin(static_cast<size_t>(cs_utf8.size()),
						 sizeof(meta->colorspace) - 1);
				memcpy(meta->colorspace, cs_utf8.constData(), copy_len);
				meta->colorspace[copy_len] = '\0';
			}
			if (!input_pool.publish(slot)) {
				qWarning() << "RenderWorkerPool failed to publish input slot";
				return JobResult::k_fatal_failure;
			}
			input_slots.append(int(slot));
		}

		if (input_slots.size() != job.input_frames.size()) {
			qWarning() << "RenderWorkerPool failed to publish all input frames;"
					   << "aborting worker render";
			return JobResult::k_fatal_failure;
		}
	}

	set_active_worker(worker_index, job.ticket, worker->process, ticket_id);
	if (job.ticket->is_cancelled()) {
		engine::internal::ipc::CancelMsg cancel;
		cancel.ticket_id = ticket_id;
		try_write_control_message(worker->process, cancel.to_json());
		clear_active_worker(worker_index, worker_process_id);
		return JobResult::k_cancelled;
	}

	engine::internal::ipc::HandshakeMsg handshake;
	handshake.protocol_version = k_protocol_version;
	handshake.shm_key = shm_key;
	handshake.input_shm_key = input_slots.isEmpty() ? QString() : input_shm_key;
	handshake.input_slots = input_slots.size();
	handshake.output_slots = int(k_output_slots);
	handshake.slot_data_bytes = qint64(output_slot_bytes);
	handshake.input_slot_data_bytes =
		input_slots.isEmpty() ? 0 : qint64(input_slot_bytes);
	if (!write_control_message(worker->process, handshake.to_json())) {
		if (!job.ticket->is_cancelled()) {
			qWarning()
				<< "RenderWorkerPool failed to send shared-memory handshake";
		}
		clear_active_worker(worker_index, worker_process_id);
		return job.ticket->is_cancelled() ? JobResult::k_cancelled :
										   JobResult::k_retryable_failure;
	}

	if (worker->loaded_graph_path != job.graph_path) {
		engine::internal::ipc::LoadGraphMsg load;
		load.path = job.graph_path;
		QString error;
		QJsonObject response;
		if (!write_control_message(worker->process, load.to_json()) ||
			!read_control_message(worker->process, &response, &error)) {
			if (!job.ticket->is_cancelled()) {
				qWarning() << "RenderWorkerPool failed to load graph in worker"
						   << error << worker->process->readAllStandardError();
			}
			clear_active_worker(worker_index, worker_process_id);
			return job.ticket->is_cancelled() ? JobResult::k_cancelled :
											   JobResult::k_retryable_failure;
		}
		worker->loaded_graph_path = job.graph_path;
	}
	engine::internal::ipc::RenderFrameMsg render;
	render.ticket_id = ticket_id;
	render.node_uuid = job.node_token;
	render.time_num = job.params.time.numerator();
	render.time_den = job.params.time.denominator();
	render.width = output_width;
	render.height = output_height;
	render.format = int(output_format);
	render.channel_count = output_channels;
	render.mode = int(job.params.mode);
	render.input_slot = input_slots.isEmpty() ? -1 : input_slots.front();
	render.input_slots = input_slots;

	const ColorTransform &ct = job.params.force_color_transform;
	render.has_color_transform = !ct.output().isEmpty();
	render.color_is_display = ct.is_display();
	render.color_output = ct.output();
	render.color_view = ct.view();
	render.color_look = ct.look();

	if (!write_control_message(worker->process, render.to_json())) {
		if (!job.ticket->is_cancelled()) {
			qWarning() << "RenderWorkerPool failed to send render_frame";
		}
		clear_active_worker(worker_index, worker_process_id);
		return job.ticket->is_cancelled() ? JobResult::k_cancelled :
										   JobResult::k_retryable_failure;
	}

	QString error;
	QJsonObject response;
	engine::internal::ipc::FrameReadyMsg ready;
	while (true) {
		if (!read_control_message(worker->process, &response, &error, 30000)) {
			if (!job.ticket->is_cancelled()) {
				qWarning() << "RenderWorkerPool failed waiting for frame_ready"
						   << error << worker->process->readAllStandardError();
			}
			clear_active_worker(worker_index, worker_process_id);
			return job.ticket->is_cancelled() ? JobResult::k_cancelled :
											   JobResult::k_retryable_failure;
		}

		if (engine::internal::ipc::FrameReadyMsg::from_json(response, &ready)) {
			break;
		}
	}

	if (job.ticket->is_cancelled()) {
		clear_active_worker(worker_index, worker_process_id);
		return JobResult::k_cancelled;
	}

	uint32_t consumed_slot = 0;
	if (!output_pool.consume(&consumed_slot)) {
		qWarning() << "RenderWorkerPool failed to consume output slot";
		clear_active_worker(worker_index, worker_process_id);
		return JobResult::k_retryable_failure;
	}
	if (int(consumed_slot) != ready.output_slot) {
		qWarning() << "RenderWorkerPool output slot mismatch: consumed"
				   << consumed_slot << "expected" << ready.output_slot;
	}
	finish_with_frame(job.ticket, output_pool, consumed_slot);
	output_pool.release(consumed_slot);
	clear_active_worker(worker_index, worker_process_id);

	return JobResult::k_finished;
}

void RenderWorkerPool::cancel_active_process(qint64 process_id)
{
	kill_process_by_id(process_id);
}

void RenderWorkerPool::set_active_worker(int worker_index, RenderTicketPtr ticket,
									   QProcess *worker, qint64 ticket_id)
{
	QMutexLocker locker(&mutex_);
	if (worker_index < 0 || worker_index >= active_jobs_.size()) {
		return;
	}

	ActiveJob &active = active_jobs_[worker_index];
	active.ticket = ticket;
	active.process_id = worker ? worker->processId() : 0;
	active.ticket_id = ticket_id;
}

void RenderWorkerPool::clear_active_worker(int worker_index, qint64 process_id)
{
	QMutexLocker locker(&mutex_);
	if (worker_index < 0 || worker_index >= active_jobs_.size()) {
		return;
	}

	ActiveJob &active = active_jobs_[worker_index];
	if (process_id > 0) {
		if (active.process_id == process_id) {
			active.process_id = 0;
		}
	} else {
		active = ActiveJob();
	}
}

int RenderWorkerPool::worker_count() const
{
	// GPU rendering is the bottleneck for video frames; too many workers just
	// multiply first-frame warmup (shader/OCIO cache creation) and compete for
	// the same GPU. Cap at a small number while still leaving cores free.
	const int ideal = QThread::idealThreadCount();
	return std::max(1, std::min(ideal - 2, 4));
}

std::unique_ptr<RenderWorkerPool::PooledWorker> RenderWorkerPool::acquire_worker(
	std::vector<std::unique_ptr<PooledWorker>> *local_pool,
	const QString &graph_path)
{
	if (!local_pool) {
		return nullptr;
	}

	const qint64 now = QDateTime::currentMSecsSinceEpoch();

	// Prefer an idle worker that already has the requested graph loaded.
	int best_index = -1;
	for (size_t i = 0; i < local_pool->size();) {
		PooledWorker *candidate = (*local_pool)[i].get();
		if (!candidate || !candidate->process) {
			local_pool->erase(local_pool->begin() + i);
			continue;
		}
		const bool candidate_state_running = candidate->process->state() ==
											 QProcess::Running;
		const bool candidate_os_alive =
			is_process_alive(candidate->process->processId());
		if (!candidate_state_running && !candidate_os_alive) {
			shutdown_worker(candidate);
			local_pool->erase(local_pool->begin() + i);
			continue;
		}
		if (now - candidate->last_used_ms > k_worker_idle_timeout_ms) {
			shutdown_worker(candidate);
			local_pool->erase(local_pool->begin() + i);
			continue;
		}
		if (best_index < 0 ||
			(!candidate->loaded_graph_path.isEmpty() &&
			 candidate->loaded_graph_path == graph_path &&
			 ((*local_pool)[size_t(best_index)]->loaded_graph_path !=
			  graph_path))) {
			best_index = int(i);
		}
		++i;
	}

	if (best_index >= 0) {
		std::unique_ptr<PooledWorker> worker =
			std::move((*local_pool)[size_t(best_index)]);
		local_pool->erase(local_pool->begin() + best_index);
		worker->last_used_ms = now;
		++worker->use_count;
		return worker;
	}

	// No idle worker available: start a new one.
	auto *process = new QProcess();
	process->setProgram(worker_program_path());
	process->setArguments({ QStringLiteral("--backend"), gpu_backend_ });

	const QString worker_stderr_path =
		QDir(QDir::tempPath())
			.filePath(QStringLiteral("oak-render-worker-%1-%2.stderr.log")
						  .arg(QCoreApplication::applicationPid())
						  .arg(QDateTime::currentMSecsSinceEpoch()));
	process->setStandardErrorFile(worker_stderr_path);

	process->start();
	if (!process->waitForStarted(10000)) {
		qWarning() << "RenderWorkerPool failed to start worker"
				   << process->errorString();
		delete process;
		return nullptr;
	}

	QString error;
	QJsonObject response;
	if (!read_control_message(process, &response, &error)) {
		qWarning() << "RenderWorkerPool did not receive startup handshake"
				   << error << process->readAllStandardError();
		process->kill();
		process->waitForFinished();
		delete process;
		return nullptr;
	}

	auto worker = std::make_unique<PooledWorker>();
	worker->process = process;
	worker->last_used_ms = now;
	worker->use_count = 1;
	return worker;
}

void RenderWorkerPool::return_worker(
	std::vector<std::unique_ptr<PooledWorker>> *local_pool,
	std::unique_ptr<PooledWorker> worker, bool keep_alive)
{
	if (!worker || !worker->process) {
		return;
	}

	const bool pool_full = worker->use_count >= k_worker_max_uses;
	if (!keep_alive || stopping_ || pool_full) {
		shutdown_worker(worker.get());
		return;
	}

	worker->last_used_ms = QDateTime::currentMSecsSinceEpoch();
	local_pool->push_back(std::move(worker));
}

void RenderWorkerPool::shutdown_worker(PooledWorker *worker)
{
	if (!worker || !worker->process) {
		return;
	}

	QProcess *process = worker->process;
	worker->process = nullptr;
	worker->loaded_graph_path.clear();
	worker->use_count = 0;

	if (process->state() == QProcess::Running) {
		QJsonObject shutdown;
		shutdown[QStringLiteral("type")] = engine::internal::ipc::msgtype::k_shutdown;
		try_write_control_message(process, shutdown);
		process->closeWriteChannel();
		if (!process->waitForFinished(5000)) {
			process->kill();
			process->waitForFinished();
		}
	}
	delete process;
}

void RenderWorkerPool::shutdown_local_pool(
	std::vector<std::unique_ptr<PooledWorker>> *local_pool)
{
	if (!local_pool) {
		return;
	}
	for (std::unique_ptr<PooledWorker> &worker : *local_pool) {
		shutdown_worker(worker.get());
	}
	local_pool->clear();
}

void RenderWorkerPool::clear_graph_cache()
{
	QMutexLocker locker(&mutex_);
	for (auto it = graph_cache_.begin(); it != graph_cache_.end(); ++it) {
		set_graph_path_cached_locked(it->path, false);
	}
	graph_cache_.clear();
	graph_path_ref_count_.clear();
	cached_graph_paths_.clear();
}

void RenderWorkerPool::finish_with_frame(RenderTicketPtr ticket,
									   const engine::internal::ipc::FrameSlotPool &pool,
									   uint32_t slot)
{
	const engine::internal::ipc::FrameSlotMeta *meta = pool.meta(slot);
	if (!meta || meta->data_size <= 0 ||
		meta->data_size > int(pool.slot_data_bytes())) {
		ticket->finish();
		return;
	}

	VideoParams params(meta->width, meta->height,
					   PixelFormat::Format(meta->format), meta->channel_count);
	FramePtr frame = Frame::create();
	frame->set_timestamp(Rational(int(meta->time_num), int(meta->time_den)));
	frame->set_video_params(params);
	if (!frame->allocate() || frame->allocated_size() < meta->data_size) {
		ticket->finish();
		return;
	}

	memcpy(frame->data(), pool.slot_data(slot), size_t(meta->data_size));
	ticket->finish(QVariant::fromValue(frame));
}

void RenderWorkerPool::cleanup_graph_file(const QString &path)
{
	if (!path.isEmpty()) {
		QFile::remove(path);
	}
}

void RenderWorkerPool::add_graph_path_ref(const QString &path)
{
	QMutexLocker locker(&mutex_);
	add_graph_path_ref_locked(path);
}

void RenderWorkerPool::add_graph_path_ref_locked(const QString &path)
{
	if (path.isEmpty()) {
		return;
	}
	++graph_path_ref_count_[path];
}

void RenderWorkerPool::release_graph_path_ref(const QString &path)
{
	QMutexLocker locker(&mutex_);
	release_graph_path_ref_locked(path);
}

void RenderWorkerPool::release_graph_path_ref_locked(const QString &path)
{
	if (path.isEmpty()) {
		return;
	}
	auto it = graph_path_ref_count_.find(path);
	if (it == graph_path_ref_count_.end()) {
		return;
	}
	if (--(*it) <= 0) {
		graph_path_ref_count_.erase(it);
		if (!cached_graph_paths_.contains(path)) {
			cleanup_graph_file(path);
		}
	}
}

void RenderWorkerPool::set_graph_path_cached(const QString &path, bool cached)
{
	QMutexLocker locker(&mutex_);
	set_graph_path_cached_locked(path, cached);
}

void RenderWorkerPool::set_graph_path_cached_locked(const QString &path,
												bool cached)
{
	if (path.isEmpty()) {
		return;
	}
	if (cached) {
		cached_graph_paths_.insert(path);
	} else {
		cached_graph_paths_.remove(path);
		if (!graph_path_ref_count_.contains(path)) {
			cleanup_graph_file(path);
		}
	}
}

} // namespace olive
