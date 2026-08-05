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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#endif

#include "codec/frame.h"
#include "paths.h"
#include "project/footage/footage.h"
#include "traverser.h"
#include "workerjson.h"
#include "xmlutils.h"
#include "ipc/frameslotpool.h"
#include "ipc/sharedmemoryregion.h"

namespace olive
{

namespace
{

constexpr int k_protocol_version = 1;

// Control-channel message type strings (worker wire protocol, unchanged).
namespace msgtype
{
constexpr const char *k_handshake = "handshake";
constexpr const char *k_load_graph = "load_graph";
constexpr const char *k_render_frame = "render_frame";
constexpr const char *k_frame_ready = "frame_ready";
constexpr const char *k_cancel = "cancel";
constexpr const char *k_shutdown = "shutdown";
constexpr const char *k_error = "error";
} // namespace msgtype

int64_t current_msecs_since_epoch()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

int64_t file_last_modified_ms(const std::string &path)
{
	std::error_code ec;
	const auto t = std::filesystem::last_write_time(path, ec);
	if (ec) {
		return 0;
	}
	const auto sys = std::chrono::time_point_cast<std::chrono::milliseconds>(
		t - decltype(t)::clock::now() + std::chrono::system_clock::now());
	return sys.time_since_epoch().count();
}

struct FootageInput {
	FootageJob job;
	Rational time;
};

class FootageInputCollector : public NodeTraverser {
public:
	std::vector<FootageInput>
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
		(void)destination;
		if (stream) {
			inputs_.push_back({ *stream, input_time });
		}
	}

private:
	std::vector<FootageInput> inputs_;
};

DecoderPtr resolve_decoder_from_cache(DecoderCache *decoder_cache,
								   const std::string &decoder_id,
								   const Decoder::CodecStream &stream)
{
	if (!decoder_cache || !stream.is_valid()) {
		return nullptr;
	}

	std::unique_lock<std::mutex> locker(decoder_cache->mutex());
	auto cache_it = decoder_cache->find(stream);
	DecoderPair decoder =
		cache_it == decoder_cache->end() ? DecoderPair() : cache_it->second;
	const int64_t file_last_modified = file_last_modified_ms(stream.filename());

	if (decoder.decoder && decoder.last_modified == file_last_modified) {
		return decoder.decoder;
	}

	decoder.decoder = Decoder::create_from_id(decoder_id);
	decoder.last_modified = file_last_modified;
	decoder_cache->insert_or_assign(stream, decoder);
	locker.unlock();

	if (!decoder.decoder || !decoder.decoder->open(stream)) {
		fprintf(stderr,
				"RenderWorkerPool failed to open decoder for %s::%d\n",
				stream.filename().c_str(), stream.stream());
		return nullptr;
	}

	return decoder.decoder;
}

FramePtr decode_input_frame(DecoderCache *decoder_cache,
						  const FootageInput &input, CancelAtom *cancel,
						  RenderMode::Mode mode)
{
	VideoParams stream_data = input.job.video_params();
	std::string filename = input.job.filename();
	std::string decoder_id = input.job.decoder();
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
		if (frame_params.colorspace().empty() &&
			!stream_data.colorspace().empty()) {
			frame_params.set_colorspace(stream_data.colorspace());
			frame->set_video_params(frame_params);
		}
	}
	return frame;
}

bool decode_input_frames(DecoderCache *decoder_cache,
					   const RenderManager::RenderVideoParams &params,
					   CancelAtom *cancel, std::vector<FramePtr> *frames)
{
	frames->clear();

	FootageInputCollector collector;
	const std::vector<FootageInput> inputs = collector.collect(params, cancel);
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
		frames->push_back(frame);
	}

	return true;
}

std::string worker_program_path()
{
#if defined(_WIN32)
	const std::string file = "oak-render-worker.exe";
#else
	const std::string file = "oak-render-worker";
#endif

	const std::string app_dir = application_dir_path();
	namespace fs = std::filesystem;
	const std::vector<std::string> candidates = {
		(fs::path(app_dir) / file).string(),
		(fs::path(app_dir) / ".." / "app" / file).lexically_normal().string(),
		(fs::path(app_dir) / ".." / "worker" / file).lexically_normal().string(),
	};

	for (const std::string &path : candidates) {
		std::error_code ec;
		if (fs::exists(path, ec)) {
			return path;
		}
	}

	if (const char *env = std::getenv("OAK_RENDER_WORKER")) {
		return env;
	}

	return candidates.front();
}

bool write_control_message(WorkerProcess *process,
						   const workerjson::Object &obj)
{
	if (!process || !process->is_running()) {
		return false;
	}

	const std::string line = obj.to_compact() + '\n';
	return process->write_all(line, 5000);
}

void try_write_control_message(WorkerProcess *process,
							   const workerjson::Object &obj)
{
	if (!process || !process->is_running()) {
		return;
	}

	const std::string line = obj.to_compact() + '\n';
	process->write_all(line, 5000);
}

bool kill_process_by_id(int64_t process_id)
{
	if (process_id <= 0) {
		return false;
	}

#if defined(_WIN32)
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

bool is_process_alive(int64_t process_id)
{
	if (process_id <= 0) {
		return false;
	}

#if defined(_WIN32)
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

std::string worker_process_details(WorkerProcess *process)
{
	if (!process) {
		return "worker process unavailable";
	}

	char buf[160];
	snprintf(buf, sizeof(buf),
			 "running=%d exit_code=%d crashed=%d error=\"%s\"",
			 int(process->is_running()), process->exit_code(),
			 int(process->crashed()), process->error_string().c_str());
	return buf;
}

bool read_control_message(WorkerProcess *process, workerjson::Object *out,
						  std::string *error, int timeout_ms = 10000)
{
	std::string line;
	while (true) {
		if (!process->read_line(&line, timeout_ms)) {
			if (error) {
				if (!process->is_running()) {
					*error = "worker exited before response: " +
							 worker_process_details(process);
				} else {
					*error = "timeout waiting for worker response: " +
							 worker_process_details(process);
				}
			}
			return false;
		}

		// Skip blank lines silently
		const size_t first = line.find_first_not_of(" \t\r");
		if (first == std::string::npos) {
			continue;
		}

		if (!workerjson::Object::parse(line.substr(first), out)) {
			if (error) {
				*error = "worker emitted malformed control JSON";
			}
			return false;
		}

		if (out->get_string("type") == msgtype::k_error) {
			if (error) {
				*error = out->get_string("message");
			}
			return false;
		}
		return true;
	}
}

} // namespace

// Holds the persistent per-worker IPC state. Defined here rather than in the
// header because the header only forward-declares the IPC types.
struct RenderWorkerPool::PooledWorker {
	WorkerProcess *process = nullptr;
	std::string loaded_graph_path;
	int64_t last_used_ms = 0;
	int use_count = 0;

	// Persistent shared memory for this worker. Reusing regions across frames
	// avoids the cost of creating/destroying large shm segments every render.
	ipc::SharedMemoryRegion output_region;
	ipc::FrameSlotPool output_pool;
	size_t output_slot_bytes = 0;
	std::string output_shm_key;

	ipc::SharedMemoryRegion input_region;
	ipc::FrameSlotPool input_pool;
	size_t input_slot_bytes = 0;
	std::string input_shm_key;
};

RenderWorkerPool::RenderWorkerPool(DecoderCache *decoder_cache,
								   const std::string &gpu_backend)
	: decoder_cache_(decoder_cache)
	, gpu_backend_(gpu_backend)
{
}

RenderWorkerPool::~RenderWorkerPool()
{
	shutdown();
}

void RenderWorkerPool::start()
{
	thread_ = std::thread([this]() { run(); });
}

bool RenderWorkerPool::submit_frame(
	RenderTicketPtr ticket, const RenderManager::RenderVideoParams &params)
{
	Job job(ticket, params);
	if (!prepare_job(ticket, params, &job)) {
		return false;
	}

	// Mark the ticket running the moment it is accepted for rendering.
	// Otherwise there is a window between dispatch and worker pickup where the
	// ticket still appears idle, and clear_single_frame_renders() -- which only
	// spares running tickets -- would cancel a frame the viewer just requested.
	ticket->start();

	std::lock_guard<std::mutex> locker(mutex_);
	queue_.push_back(job);
	wait_.notify_one();
	return true;
}

bool RenderWorkerPool::remove_ticket(RenderTicketPtr ticket)
{
	if (!ticket) {
		return false;
	}

	std::string queued_graph_path;
	bool matched_active = false;

	{
		std::lock_guard<std::mutex> locker(mutex_);
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

	if (!queued_graph_path.empty()) {
		release_graph_path_ref(queued_graph_path);
		return true;
	}

	return true;
}

void RenderWorkerPool::shutdown()
{
	{
		std::lock_guard<std::mutex> locker(mutex_);
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
			set_graph_path_cached_locked(it->second.path, false);
		}
		graph_cache_.clear();
		wait_.notify_all();
	}

	if (thread_.joinable()) {
		thread_.join();
	}
}

void RenderWorkerPool::run()
{
	const int count = worker_count();
	{
		std::lock_guard<std::mutex> locker(mutex_);
		active_jobs_.resize(size_t(count));
	}

	auto local_pools =
		std::vector<std::vector<std::unique_ptr<PooledWorker>>>(size_t(count));
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

	std::lock_guard<std::mutex> locker(mutex_);
	active_jobs_.clear();
}

void RenderWorkerPool::worker_loop(
	int worker_index, std::vector<std::unique_ptr<PooledWorker>> *local_pool)
{
	while (true) {
		std::unique_lock<std::mutex> locker(mutex_);
		while (queue_.empty() && !stopping_) {
			wait_.wait(locker);
		}
		if (stopping_ && queue_.empty()) {
			break;
		}

		Job job = queue_.front();
		queue_.pop_front();
		locker.unlock();

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
		fprintf(stderr,
				"RenderWorkerPool could not resolve project for render node\n");
		return false;
	}

	std::vector<FramePtr> input_frames;
	if (!decode_input_frames(decoder_cache_, params, ticket->get_cancel_atom(),
						   &input_frames)) {
		fprintf(stderr,
				"RenderWorkerPool could not predecode footage inputs; falling "
				"back to in-process render\n");
		return false;
	}

	std::string graph_path;
	bool wrote_new_snapshot = false;
	{
		const std::string project_uuid = project->get_uuid();
		std::unique_lock<std::mutex> locker(mutex_);
		auto it = graph_cache_.find(project_uuid);
		if (it != graph_cache_.end() && !project->is_modified()) {
			graph_path = it->second.path;
			add_graph_path_ref_locked(graph_path);
		} else {
			if (it != graph_cache_.end()) {
				set_graph_path_cached_locked(it->second.path, false);
				graph_cache_.erase(it);
			}
			locker.unlock();
			if (!write_graph_snapshot(project, &graph_path)) {
				return false;
			}
			wrote_new_snapshot = true;
			// For internal render-proxy copies, the snapshot now represents the
			// serialized state, so reset the modified flag so the snapshot can be
			// reused until the copy changes again. (The proxy marker was a Qt
			// dynamic property "_oak_render_proxy"; ProjectCopier now stores it
			// as a project setting.)
			if (project->get_setting("_oak_render_proxy") == "1") {
				project->set_modified(false);
			}
			locker.lock();
			graph_cache_.insert({ project_uuid, { graph_path } });
			set_graph_path_cached_locked(graph_path, true);
			add_graph_path_ref_locked(graph_path);
		}
	}

	job->ticket = ticket;
	job->params = params;
	job->graph_path = graph_path;
	job->node_token =
		std::to_string(reinterpret_cast<uintptr_t>(params.node));
	job->input_frames = input_frames;
	(void)wrote_new_snapshot;
	return true;
}

bool RenderWorkerPool::write_graph_snapshot(Project *project, std::string *path)
{
	// Keep snapshots in the system temp directory. The previous bug was not the
	// temp location itself, but stale snapshots being deleted while queued jobs
	// still referenced them.
	namespace fs = std::filesystem;
	const std::string graph_dir = temp_dir_path();

	// mkstemps: unique file like QTemporaryFile's XXXXXX template, kept on disk
	std::string tmpl =
		(fs::path(graph_dir) / "oak-render-graph-XXXXXX.ove").string();
	std::vector<char> tmpl_buf(tmpl.begin(), tmpl.end());
	tmpl_buf.push_back('\0');
#if defined(_WIN32)
	// Poor man's unique name; mkstemps is POSIX-only
	const std::string snapshot_path =
		(fs::path(graph_dir) /
		 ("oak-render-graph-" + std::to_string(application_pid()) + "-" +
		  std::to_string(current_msecs_since_epoch()) + ".ove"))
			.string();
	std::ofstream file(snapshot_path, std::ios::binary | std::ios::trunc);
	if (!file) {
		fprintf(stderr,
				"RenderWorkerPool failed to create graph snapshot temp file\n");
		return false;
	}
#else
	const int fd = mkstemps(tmpl_buf.data(), 4);
	if (fd < 0) {
		fprintf(stderr,
				"RenderWorkerPool failed to create graph snapshot temp file: "
				"%s\n",
				strerror(errno));
		return false;
	}
	const std::string snapshot_path = tmpl_buf.data();
	std::ofstream file(snapshot_path, std::ios::binary | std::ios::trunc);
	if (!file) {
		fprintf(stderr,
				"RenderWorkerPool failed to open graph snapshot temp file\n");
		close(fd);
		fs::remove(snapshot_path);
		return false;
	}
	close(fd);
#endif

	XmlStreamWriter writer;
	ProjectSerializer::SaveData data(ProjectSerializer::k_project, project,
									 snapshot_path);
	const ProjectSerializer::Result result =
		ProjectSerializer::save(&writer, data);
	file << writer.output();
	file.close();

	if (result.code() != ProjectSerializer::k_success || !file) {
		fprintf(stderr,
				"RenderWorkerPool failed to serialize graph snapshot %s\n",
				result.get_details().c_str());
		fs::remove(snapshot_path);
		return false;
	}

	*path = snapshot_path;
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
	const int64_t ticket_id =
		int64_t(reinterpret_cast<uintptr_t>(job.ticket.get()));
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
		fprintf(stderr,
				"RenderWorkerPool failed to acquire worker for ticket %lld\n",
				(long long)ticket_id);
		job.ticket->finish();
		clear_active_worker(worker_index, 0);
		return;
	}

	for (int attempt = 0; attempt < k_max_attempts; attempt++) {
		if (attempt > 0) {
			worker = acquire_worker(local_pool, job.graph_path);
			if (!worker) {
				fprintf(stderr,
						"RenderWorkerPool failed to acquire worker for retry "
						"%lld\n",
						(long long)ticket_id);
				break;
			}
		}

		const JobResult result =
			process_job_attempt(job, worker_index, attempt, worker.get());
		const int64_t worker_pid =
			worker && worker->process ? worker->process->process_id() : 0;
		const bool process_running = worker && worker->process &&
									 worker->process->is_running();
		const bool os_alive = worker_pid > 0 && is_process_alive(worker_pid);
		const bool worker_healthy = process_running || os_alive;
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
			fprintf(stderr,
					"RenderWorkerPool retrying render worker for ticket %lld "
					"after worker failure\n",
					(long long)ticket_id);
		}
	}

	if (job.ticket->is_cancelled()) {
		job.ticket->finish();
	} else {
		fprintf(stderr,
				"RenderWorkerPool exhausted worker retries for ticket %lld\n",
				(long long)ticket_id);
		job.ticket->finish();
	}
	clear_active_worker(worker_index, 0);
}

RenderWorkerPool::JobResult
RenderWorkerPool::process_job_attempt(const Job &job, int worker_index,
									int attempt_index, PooledWorker *worker)
{
	const int64_t ticket_id =
		int64_t(reinterpret_cast<uintptr_t>(job.ticket.get()));
	if (job.ticket->is_cancelled()) {
		return JobResult::k_cancelled;
	}

	if (!worker || !worker->process) {
		return JobResult::k_retryable_failure;
	}

	const int64_t worker_process_id = worker->process->process_id();

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
		ipc::FrameSlotPool::bytes_needed(k_output_slots, output_slot_bytes);

	if (!worker->output_region.is_valid() ||
		worker->output_slot_bytes < output_slot_bytes) {
		if (worker->output_region.is_valid()) {
			worker->output_region.close();
			worker->output_pool = ipc::FrameSlotPool();
		}
		if (worker->output_shm_key.empty()) {
			worker->output_shm_key =
				ipc::SharedMemoryRegion::make_key(worker_process_id, 0) +
				"-out";
		}
		if (!worker->output_region.open(worker->output_shm_key,
										output_region_bytes,
										ipc::SharedMemoryRegion::k_create)) {
			fprintf(stderr,
					"RenderWorkerPool failed to create output shared memory: "
					"%s\n",
					worker->output_region.error().c_str());
			return JobResult::k_fatal_failure;
		}
		worker->output_pool = ipc::FrameSlotPool::create(
			worker->output_region.data(), k_output_slots, output_slot_bytes);
		worker->output_slot_bytes = output_slot_bytes;
	}
	const std::string shm_key = worker->output_shm_key;
	ipc::FrameSlotPool &output_pool = worker->output_pool;

	const uint32_t input_slot_count =
		job.input_frames.empty() ? 0 : uint32_t(job.input_frames.size());
	if (input_slot_count > 0) {
		if (!worker->input_region.is_valid() ||
			worker->input_slot_bytes < input_slot_bytes ||
			worker->input_pool.slot_count() < input_slot_count) {
			if (worker->input_region.is_valid()) {
				worker->input_region.close();
				worker->input_pool = ipc::FrameSlotPool();
			}
			if (worker->input_shm_key.empty()) {
				worker->input_shm_key =
					ipc::SharedMemoryRegion::make_key(worker_process_id, 1) +
					"-in";
			}
			const size_t input_region_bytes =
				ipc::FrameSlotPool::bytes_needed(input_slot_count,
												 input_slot_bytes);
			if (!worker->input_region.open(worker->input_shm_key,
										   input_region_bytes,
										   ipc::SharedMemoryRegion::k_create)) {
				fprintf(stderr,
						"RenderWorkerPool failed to create input shared "
						"memory: %s\n",
						worker->input_region.error().c_str());
				return JobResult::k_fatal_failure;
			}
			worker->input_pool =
				ipc::FrameSlotPool::create(worker->input_region.data(),
										   input_slot_count, input_slot_bytes);
			worker->input_slot_bytes = input_slot_bytes;
		}
	}
	const std::string input_shm_key = worker->input_shm_key;
	ipc::FrameSlotPool &input_pool = worker->input_pool;
	std::vector<int> input_slots;
	if (input_slot_count > 0) {
		for (const FramePtr &frame : job.input_frames) {
			if (frame->allocated_size() > int64_t(worker->input_slot_bytes)) {
				fprintf(stderr,
						"RenderWorkerPool decoded input frame exceeds slot "
						"size\n");
				return JobResult::k_fatal_failure;
			}

			uint32_t slot = 0;
			if (!input_pool.acquire(&slot)) {
				fprintf(stderr, "RenderWorkerPool input pool had no free slot\n");
				return JobResult::k_fatal_failure;
			}

			memcpy(input_pool.slot_data(slot), frame->const_data(),
				   size_t(frame->allocated_size()));
			ipc::FrameSlotMeta *meta = input_pool.meta(slot);
			meta->id = int64_t(input_slots.size());
			meta->time_num = frame->timestamp().numerator();
			meta->time_den = frame->timestamp().denominator();
			meta->width = frame->width();
			meta->height = frame->height();
			meta->format = int32_t(frame->format());
			meta->channel_count = frame->channel_count();
			meta->linesize = frame->linesize_bytes();
			meta->data_size = frame->allocated_size();
			memset(meta->colorspace, 0, sizeof(meta->colorspace));
			const std::string cs = frame->video_params().colorspace();
			if (!cs.empty()) {
				const size_t copy_len =
					std::min(cs.size(), sizeof(meta->colorspace) - 1);
				memcpy(meta->colorspace, cs.data(), copy_len);
				meta->colorspace[copy_len] = '\0';
			}
			if (!input_pool.publish(slot)) {
				fprintf(stderr,
						"RenderWorkerPool failed to publish input slot\n");
				return JobResult::k_fatal_failure;
			}
			input_slots.push_back(int(slot));
		}

		if (input_slots.size() != job.input_frames.size()) {
			fprintf(stderr,
					"RenderWorkerPool failed to publish all input frames; "
					"aborting worker render\n");
			return JobResult::k_fatal_failure;
		}
	}

	set_active_worker(worker_index, job.ticket, worker->process, ticket_id);
	if (job.ticket->is_cancelled()) {
		workerjson::Object cancel;
		cancel.set_string("type", msgtype::k_cancel);
		cancel.set_int("ticket", ticket_id);
		try_write_control_message(worker->process, cancel);
		clear_active_worker(worker_index, worker_process_id);
		return JobResult::k_cancelled;
	}

	workerjson::Object handshake;
	handshake.set_string("type", msgtype::k_handshake);
	handshake.set_int("protocol_version", k_protocol_version);
	handshake.set_string("shm_key", shm_key);
	handshake.set_string("input_shm_key",
						 input_slots.empty() ? std::string() : input_shm_key);
	handshake.set_int("input_slots", int64_t(input_slots.size()));
	handshake.set_int("output_slots", int64_t(k_output_slots));
	handshake.set_int("slot_data_bytes", int64_t(output_slot_bytes));
	handshake.set_int("input_slot_data_bytes",
					  input_slots.empty() ? 0 : int64_t(input_slot_bytes));
	if (!write_control_message(worker->process, handshake)) {
		if (!job.ticket->is_cancelled()) {
			fprintf(stderr,
					"RenderWorkerPool failed to send shared-memory handshake\n");
		}
		clear_active_worker(worker_index, worker_process_id);
		return job.ticket->is_cancelled() ? JobResult::k_cancelled :
										   JobResult::k_retryable_failure;
	}

	if (worker->loaded_graph_path != job.graph_path) {
		workerjson::Object load;
		load.set_string("type", msgtype::k_load_graph);
		load.set_string("path", job.graph_path);
		std::string error;
		workerjson::Object response;
		if (!write_control_message(worker->process, load) ||
			!read_control_message(worker->process, &response, &error)) {
			if (!job.ticket->is_cancelled()) {
				fprintf(stderr,
						"RenderWorkerPool failed to load graph in worker: %s\n",
						error.c_str());
			}
			clear_active_worker(worker_index, worker_process_id);
			return job.ticket->is_cancelled() ? JobResult::k_cancelled :
											   JobResult::k_retryable_failure;
		}
		worker->loaded_graph_path = job.graph_path;
	}

	workerjson::Object render;
	render.set_string("type", msgtype::k_render_frame);
	render.set_int("ticket", ticket_id);
	render.set_string("node", job.node_token);
	render.set_int("time_num", job.params.time.numerator());
	render.set_int("time_den", job.params.time.denominator());
	render.set_int("width", output_width);
	render.set_int("height", output_height);
	render.set_int("format", int64_t(output_format));
	render.set_int("channels", output_channels);
	render.set_int("mode", int64_t(job.params.mode));
	render.set_int("input_slot", input_slots.empty() ? -1 : input_slots.front());
	render.set_int_array("input_slots", input_slots);

	const ColorTransform &ct = job.params.force_color_transform;
	if (!ct.output().empty()) {
		render.set_bool("has_color_transform", true);
		render.set_bool("color_is_display", ct.is_display());
		render.set_string("color_output", ct.output());
		render.set_string("color_view", ct.view());
		render.set_string("color_look", ct.look());
	}

	if (!write_control_message(worker->process, render)) {
		if (!job.ticket->is_cancelled()) {
			fprintf(stderr, "RenderWorkerPool failed to send render_frame\n");
		}
		clear_active_worker(worker_index, worker_process_id);
		return job.ticket->is_cancelled() ? JobResult::k_cancelled :
										   JobResult::k_retryable_failure;
	}

	std::string error;
	workerjson::Object response;
	while (true) {
		if (!read_control_message(worker->process, &response, &error, 30000)) {
			if (!job.ticket->is_cancelled()) {
				fprintf(stderr,
						"RenderWorkerPool failed waiting for frame_ready: %s\n",
						error.c_str());
			}
			clear_active_worker(worker_index, worker_process_id);
			return job.ticket->is_cancelled() ? JobResult::k_cancelled :
											   JobResult::k_retryable_failure;
		}

		if (response.get_string("type") == msgtype::k_frame_ready) {
			break;
		}
	}

	if (job.ticket->is_cancelled()) {
		clear_active_worker(worker_index, worker_process_id);
		return JobResult::k_cancelled;
	}

	const int ready_output_slot = int(response.get_int("slot"));

	uint32_t consumed_slot = 0;
	if (!output_pool.consume(&consumed_slot)) {
		fprintf(stderr, "RenderWorkerPool failed to consume output slot\n");
		clear_active_worker(worker_index, worker_process_id);
		return JobResult::k_retryable_failure;
	}
	if (int(consumed_slot) != ready_output_slot) {
		fprintf(stderr,
				"RenderWorkerPool output slot mismatch: consumed %u expected "
				"%d\n",
				consumed_slot, ready_output_slot);
	}
	finish_with_frame(job.ticket, output_pool, consumed_slot);
	output_pool.release(consumed_slot);
	clear_active_worker(worker_index, worker_process_id);

	return JobResult::k_finished;
}

void RenderWorkerPool::cancel_active_process(int64_t process_id)
{
	kill_process_by_id(process_id);
}

void RenderWorkerPool::set_active_worker(int worker_index,
									 RenderTicketPtr ticket,
									 WorkerProcess *worker, int64_t ticket_id)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (worker_index < 0 || worker_index >= int(active_jobs_.size())) {
		return;
	}

	ActiveJob &active = active_jobs_[size_t(worker_index)];
	active.ticket = ticket;
	active.process_id = worker ? worker->process_id() : 0;
	active.ticket_id = ticket_id;
}

void RenderWorkerPool::clear_active_worker(int worker_index, int64_t process_id)
{
	std::lock_guard<std::mutex> locker(mutex_);
	if (worker_index < 0 || worker_index >= int(active_jobs_.size())) {
		return;
	}

	ActiveJob &active = active_jobs_[size_t(worker_index)];
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
	const int ideal = int(std::thread::hardware_concurrency());
	return std::max(1, std::min(ideal - 2, 4));
}

std::unique_ptr<RenderWorkerPool::PooledWorker> RenderWorkerPool::acquire_worker(
	std::vector<std::unique_ptr<PooledWorker>> *local_pool,
	const std::string &graph_path)
{
	if (!local_pool) {
		return nullptr;
	}

	const int64_t now = current_msecs_since_epoch();

	// Prefer an idle worker that already has the requested graph loaded.
	int best_index = -1;
	for (size_t i = 0; i < local_pool->size();) {
		PooledWorker *candidate = (*local_pool)[i].get();
		if (!candidate || !candidate->process) {
			local_pool->erase(local_pool->begin() + i);
			continue;
		}
		const bool candidate_running = candidate->process->is_running();
		const bool candidate_os_alive =
			is_process_alive(candidate->process->process_id());
		if (!candidate_running && !candidate_os_alive) {
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
			(!candidate->loaded_graph_path.empty() &&
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
	auto *process = new WorkerProcess();

	// The engine defaults QT_QPA_PLATFORM to "offscreen" for headless hosts
	// (cli/tests), but the worker needs a real platform GL context. Don't let
	// it inherit the offscreen default from this process.
	std::vector<std::string> remove_env;
	if (const char *qpa = std::getenv("QT_QPA_PLATFORM")) {
		if (std::string(qpa) == "offscreen") {
			remove_env.push_back("QT_QPA_PLATFORM");
		}
	}

	namespace fs = std::filesystem;
	const std::string worker_stderr_path =
		(fs::path(temp_dir_path()) /
		 ("oak-render-worker-" + std::to_string(application_pid()) + "-" +
		  std::to_string(current_msecs_since_epoch()) + ".stderr.log"))
			.string();

	if (!process->start(worker_program_path(),
						{ "--backend", gpu_backend_ }, remove_env,
						worker_stderr_path)) {
		fprintf(stderr, "RenderWorkerPool failed to start worker: %s\n",
				process->error_string().c_str());
		delete process;
		return nullptr;
	}

	std::string error;
	workerjson::Object response;
	if (!read_control_message(process, &response, &error)) {
		fprintf(stderr,
				"RenderWorkerPool did not receive startup handshake: %s\n",
				error.c_str());
		process->kill();
		process->wait_finished(-1);
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

	worker->last_used_ms = current_msecs_since_epoch();
	local_pool->push_back(std::move(worker));
}

void RenderWorkerPool::shutdown_worker(PooledWorker *worker)
{
	if (!worker || !worker->process) {
		return;
	}

	WorkerProcess *process = worker->process;
	worker->process = nullptr;
	worker->loaded_graph_path.clear();
	worker->use_count = 0;

	if (process->is_running()) {
		workerjson::Object shutdown;
		shutdown.set_string("type", msgtype::k_shutdown);
		try_write_control_message(process, shutdown);
		process->close_write_channel();
		if (!process->wait_finished(5000)) {
			process->kill();
			process->wait_finished(-1);
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
	std::lock_guard<std::mutex> locker(mutex_);
	for (auto it = graph_cache_.begin(); it != graph_cache_.end(); ++it) {
		set_graph_path_cached_locked(it->second.path, false);
	}
	graph_cache_.clear();
	graph_path_ref_count_.clear();
	cached_graph_paths_.clear();
}

void RenderWorkerPool::finish_with_frame(RenderTicketPtr ticket,
									   const ipc::FrameSlotPool &pool,
									   uint32_t slot)
{
	const ipc::FrameSlotMeta *meta = pool.meta(slot);
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
	ticket->finish(Variant::from_value(frame));
}

void RenderWorkerPool::cleanup_graph_file(const std::string &path)
{
	if (!path.empty()) {
		std::error_code ec;
		std::filesystem::remove(path, ec);
	}
}

void RenderWorkerPool::add_graph_path_ref(const std::string &path)
{
	std::lock_guard<std::mutex> locker(mutex_);
	add_graph_path_ref_locked(path);
}

void RenderWorkerPool::add_graph_path_ref_locked(const std::string &path)
{
	if (path.empty()) {
		return;
	}
	++graph_path_ref_count_[path];
}

void RenderWorkerPool::release_graph_path_ref(const std::string &path)
{
	std::lock_guard<std::mutex> locker(mutex_);
	release_graph_path_ref_locked(path);
}

void RenderWorkerPool::release_graph_path_ref_locked(const std::string &path)
{
	if (path.empty()) {
		return;
	}
	auto it = graph_path_ref_count_.find(path);
	if (it == graph_path_ref_count_.end()) {
		return;
	}
	if (--(it->second) <= 0) {
		graph_path_ref_count_.erase(it);
		if (cached_graph_paths_.find(path) == cached_graph_paths_.end()) {
			cleanup_graph_file(path);
		}
	}
}

void RenderWorkerPool::set_graph_path_cached(const std::string &path, bool cached)
{
	std::lock_guard<std::mutex> locker(mutex_);
	set_graph_path_cached_locked(path, cached);
}

void RenderWorkerPool::set_graph_path_cached_locked(const std::string &path,
													bool cached)
{
	if (path.empty()) {
		return;
	}
	if (cached) {
		cached_graph_paths_.insert(path);
	} else {
		cached_graph_paths_.erase(path);
		if (graph_path_ref_count_.find(path) == graph_path_ref_count_.end()) {
			cleanup_graph_file(path);
		}
	}
}

} // namespace olive
