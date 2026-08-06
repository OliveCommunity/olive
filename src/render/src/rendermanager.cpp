/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "rendermanager.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>

#include "configaccessor.h"
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "backend/dynamicrenderer.h"
#endif
#include "opengl/openglrenderer.h"
#include "renderprocessor.h"
#include "renderworkerpool.h"

namespace olive
{

RenderManager *RenderManager::instance_ = nullptr;
const Rational RenderManager::k_dry_run_interval = Rational(10);

static int64_t current_msecs_since_epoch()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
		.count();
}

static std::string to_lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	return s;
}

RenderManager::Backend RenderManager::backend_from_string(const std::string &backend)
{
	const std::string lower = to_lower(backend);
	if (lower == "vulkan") {
		return k_vulkan;
	}

	if (lower == "multiprocess") {
		return k_multi_process;
	}

	if (lower == "dummy") {
		return k_dummy;
	}

	return k_open_gl;
}

std::string RenderManager::backend_to_string(Backend backend)
{
	switch (backend) {
	case k_open_gl:
		return "opengl";
	case k_vulkan:
		return "vulkan";
	case k_multi_process:
		return "multiprocess";
	case k_dummy:
		return "dummy";
	}

	return "opengl";
}

RenderManager::RenderManager()
	: backend_(backend_from_string(OAK_CONFIG("GraphicsBackend").toString()))
	, requested_backend_(backend_)
	, aggressive_gc_(0)
	, worker_pool_(nullptr)
{
	if (backend_ == k_vulkan) {
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		fprintf(stderr,
				"Vulkan backend requested but dynamic render backend is not "
				"enabled. Falling back to OpenGL.\n");
		// NOTE: the Qt original assigned the misspelled `kOpenGL` here, which
		// could never have compiled in this branch; k_open_gl is the intent.
		backend_ = k_open_gl;
#endif
	}

	if (backend_ == k_open_gl || backend_ == k_vulkan) {
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		auto *dynamic_renderer =
			new DynamicRenderer(backend_to_string(requested_backend_));
		if (!dynamic_renderer->load()) {
			fprintf(stderr,
					"Failed to load dynamic render backend %s, falling back to "
					"OpenGL\n",
					backend_to_string(requested_backend_).c_str());
			delete dynamic_renderer;
			backend_ = k_open_gl;
			context_ = new OpenGLRenderer();
		} else {
			context_ = dynamic_renderer;
			// DynamicRenderer may internally fall back (e.g. Vulkan -> OpenGL).
			// Synchronize RenderManager's view of the actual runtime backend.
			Backend actual_backend =
				backend_from_string(dynamic_renderer->backend_name());
			if (actual_backend != backend_) {
				fprintf(stderr,
						"Dynamic render backend fell back from %s to %s\n",
						backend_to_string(backend_).c_str(),
						backend_to_string(actual_backend).c_str());
				backend_ = actual_backend;
			}
		}
#else
		context_ = new OpenGLRenderer();
#endif
		decoder_cache_ = new DecoderCache();
		shader_cache_ = new ShaderCache();
	} else {
		fprintf(stderr, "Tried to initialize unknown graphics backend\n");
		context_ = nullptr;
		decoder_cache_ = nullptr;
	}

	if (context_) {
		dry_run_thread_ = create_thread();
		audio_thread_ = create_thread();

		waveform_threads_.resize(std::thread::hardware_concurrency());
		for (size_t i = 0; i < waveform_threads_.size(); i++) {
			waveform_threads_[i] = create_thread();
		}

		auto_cacher_ = new PreviewAutoCacher();

		worker_pool_ = new RenderWorkerPool(decoder_cache_,
											backend_to_string(requested_backend_));
		worker_pool_->start();
		backend_ = k_multi_process;
	}

	decoder_clear_interval_ms_ = k_decoder_maximum_inactivity;
	decoder_clear_thread_ = std::thread([this]() { decoder_clear_loop(); });
}

RenderManager::~RenderManager()
{
	{
		std::lock_guard<std::mutex> locker(decoder_clear_mutex_);
		decoder_clear_stopping_ = true;
	}
	decoder_clear_cv_.notify_all();
	if (decoder_clear_thread_.joinable()) {
		decoder_clear_thread_.join();
	}

	if (context_) {
		if (worker_pool_) {
			worker_pool_->shutdown();
			delete worker_pool_;
			worker_pool_ = nullptr;
		}

		delete shader_cache_;
		delete decoder_cache_;

		for (RenderThread *rt : render_threads_) {
			rt->quit();
			rt->wait();
			delete rt;
		}

		context_->post_destroy();
		delete context_;
	}
}

void RenderManager::decoder_clear_loop()
{
	std::unique_lock<std::mutex> locker(decoder_clear_mutex_);
	while (!decoder_clear_stopping_) {
		decoder_clear_cv_.wait_for(
			locker, std::chrono::milliseconds(decoder_clear_interval_ms_.load()));
		if (decoder_clear_stopping_) {
			break;
		}
		locker.unlock();
		clear_old_decoders();
		locker.lock();
	}
}

RenderThread *RenderManager::create_thread(Renderer *renderer)
{
	auto t = new RenderThread(renderer, decoder_cache_, shader_cache_);
	render_threads_.push_back(t);
	t->start();
	return t;
}

RenderTicketPtr RenderManager::render_frame(const RenderVideoParams &params)
{
	// Create ticket
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();

	ticket->set_property("node", Variant::from_value(params.node));
	ticket->set_property("time", Variant::from_value(params.time));
	ticket->set_property("size", Variant::from_value(params.force_size));
	ticket->set_property("matrix", Variant::from_value(params.force_matrix));
	ticket->set_property("format", int64_t(params.force_format));
	ticket->set_property("usecache", params.use_cache);
	ticket->set_property("channelcount", int64_t(params.force_channel_count));
	ticket->set_property("mode", int64_t(params.mode));
	ticket->set_property("type", int64_t(k_type_video));
	ticket->set_property("colormanager",
						 Variant::from_value(params.color_manager));
	ticket->set_property("coloroutput",
						 Variant::from_value(params.force_color_output));
	ticket->set_property("colortransform",
						 Variant::from_value(params.force_color_transform));
	assert(params.video_params.is_valid());
	ticket->set_property("vparam", Variant::from_value(params.video_params));
	ticket->set_property("aparam", Variant::from_value(params.audio_params));
	ticket->set_property("return", int64_t(params.return_type));
	ticket->set_property("cache", params.cache_dir);
	ticket->set_property("cachetimebase",
						 Variant::from_value(params.cache_timebase));
	ticket->set_property("cacheid", Variant::from_value(params.cache_id));
	ticket->set_property("multicam", Variant::from_value(params.multicam));

	// Video frames are always rendered by the worker pool. GPU textures cannot
	// be shared across the process boundary (or across independent Vulkan
	// instances), so texture-return requests are downgraded to CPU frames.
	RenderVideoParams worker_params = params;
	if (worker_params.return_type == ReturnType::k_texture) {
		worker_params.return_type = ReturnType::k_frame;
	}

	if (worker_params.return_type == ReturnType::k_null) {
		if (dry_run_thread_) {
			dry_run_thread_->add_ticket(ticket);
		} else {
			// No render threads (e.g. dummy backend), finish without a result
			ticket->finish();
		}
	} else if (worker_pool_ &&
			   worker_pool_->submit_frame(ticket, worker_params)) {
		return ticket;
	} else {
		fprintf(stderr,
				"RenderManager: worker pool unavailable, finishing ticket "
				"without result\n");
		ticket->finish();
	}

	return ticket;
}

RenderTicketPtr RenderManager::render_audio(const RenderAudioParams &params)
{
	// Create ticket
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();

	ticket->set_property("node", Variant::from_value(params.node));
	ticket->set_property("time", Variant::from_value(params.range));
	ticket->set_property("type", int64_t(k_type_audio));
	ticket->set_property("enablewaveforms", params.generate_waveforms);
	ticket->set_property("clamp", params.clamp);
	ticket->set_property("aparam", Variant::from_value(params.audio_params));
	ticket->set_property("mode", int64_t(params.mode));

	if (params.generate_waveforms && !waveform_threads_.empty()) {
		size_t thread_index = last_waveform_thread_ % waveform_threads_.size();
		RenderThread *thread = waveform_threads_[thread_index];
		thread->add_ticket(ticket);
		last_waveform_thread_++;
	} else if (audio_thread_) {
		audio_thread_->add_ticket(ticket);
	} else {
		// No render threads (e.g. dummy backend), finish without a result
		ticket->finish();
	}

	return ticket;
}

bool RenderManager::remove_ticket(RenderTicketPtr ticket)
{
	if (worker_pool_ && worker_pool_->remove_ticket(ticket)) {
		return true;
	}

	for (RenderThread *rt : render_threads_) {
		if (rt->remove_ticket(ticket)) {
			return true;
		}
	}

	return false;
}

void RenderManager::set_aggressive_garbage_collection(bool enabled)
{
	aggressive_gc_ += enabled ? 1 : -1;

	// Clamp at zero so unbalanced disable calls can't drive the counter negative
	if (aggressive_gc_ < 0) {
		aggressive_gc_ = 0;
	}

	if (aggressive_gc_ > 0) {
		decoder_clear_interval_ms_ = k_decoder_maximum_inactivity_aggressive;
	} else {
		decoder_clear_interval_ms_ = k_decoder_maximum_inactivity;
	}
}

void RenderManager::clear_old_decoders()
{
	if (!decoder_cache_) {
		// No decoder cache exists on backends without a renderer (e.g. dummy)
		return;
	}

	std::lock_guard<std::mutex> locker(decoder_cache_->mutex());

	int64_t min_age =
		current_msecs_since_epoch() - k_decoder_maximum_inactivity;

	for (auto it = decoder_cache_->begin(); it != decoder_cache_->end();) {
		DecoderPair decoder = it->second;

		if (decoder.decoder->get_last_accessed_time() < min_age) {
			decoder.decoder->close();
			it = decoder_cache_->erase(it);
		} else {
			it++;
		}
	}
}

RenderThread::RenderThread(Renderer *renderer, DecoderCache *decoder_cache,
						   ShaderCache *shader_cache)
	: cancelled_(false)
	, context_(renderer)
	, decoder_cache_(decoder_cache)
	, shader_cache_(shader_cache)
{
	if (context_) {
		context_->init();
	}
}

RenderThread::~RenderThread()
{
	quit();
	wait();
}

void RenderThread::start()
{
	thread_ = std::thread([this]() { run(); });
}

void RenderThread::add_ticket(RenderTicketPtr ticket)
{
	std::lock_guard<std::mutex> locker(mutex_);
	queue_.push_back(ticket);
	wait_.notify_one();
}

bool RenderThread::remove_ticket(RenderTicketPtr ticket)
{
	std::lock_guard<std::mutex> locker(mutex_);

	auto it = std::find(queue_.begin(), queue_.end(), ticket);
	if (it == queue_.end()) {
		return false;
	}

	queue_.erase(it);
	return true;
}

void RenderThread::quit()
{
	std::lock_guard<std::mutex> locker(mutex_);
	cancelled_ = true;
	wait_.notify_one();
}

void RenderThread::wait()
{
	if (thread_.joinable()) {
		thread_.join();
	}
}

void RenderThread::run()
{
	if (context_) {
		context_->post_init();
		// Replaces moveToThread(this): the renderer now belongs to this thread.
		context_->set_owner_thread_to_current();
	}

	std::unique_lock<std::mutex> locker(mutex_);

	while (!cancelled_) {
		if (queue_.empty()) {
			wait_.wait(locker);
		}

		if (cancelled_) {
			break;
		}

		if (!queue_.empty()) {
			RenderTicketPtr ticket = queue_.front();
			queue_.pop_front();

			locker.unlock();

			// Setup the ticket for ::Process
			ticket->start();

			if (ticket->is_cancelled()) {
				ticket->finish();
			} else {
				RenderProcessor::process(ticket, context_, decoder_cache_,
										 shader_cache_);
			}

			locker.lock();
		}
	}

	if (context_) {
		context_->destroy();
		// Replaces moveToThread back to the creating thread.
		context_->clear_owner_thread();
	}
}

}
