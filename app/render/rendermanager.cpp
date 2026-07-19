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

#include <QApplication>
#include <QMatrix4x4>
#include <QThread>

#include "config/config.h"
#include "core.h"
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#endif
#include "render/opengl/openglrenderer.h"
#include "renderprocessor.h"
#include "renderworkerpool.h"
#include "task/conform/conform.h"
#include "task/taskmanager.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

RenderManager *RenderManager::instance_ = nullptr;
const Rational RenderManager::k_dry_run_interval = Rational(10);

RenderManager::Backend RenderManager::backend_from_string(const QString &backend)
{
	const QString lower = backend.toLower();
	if (lower == QStringLiteral("vulkan")) {
		return k_vulkan;
	}

	if (lower == QStringLiteral("multiprocess")) {
		return k_multi_process;
	}

	if (lower == QStringLiteral("dummy")) {
		return k_dummy;
	}

	return k_open_gl;
}

QString RenderManager::backend_to_string(Backend backend)
{
	switch (backend) {
	case k_open_gl:
		return QStringLiteral("opengl");
	case k_vulkan:
		return QStringLiteral("vulkan");
	case k_multi_process:
		return QStringLiteral("multiprocess");
	case k_dummy:
		return QStringLiteral("dummy");
	}

	return QStringLiteral("opengl");
}

RenderManager::RenderManager(QObject *parent)
	: backend_(backend_from_string(OAK_CONFIG("GraphicsBackend").toString()))
	, requested_backend_(backend_)
	, aggressive_gc_(0)
	, worker_pool_(nullptr)
{
	if (backend_ == k_vulkan) {
#ifndef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		qWarning()
			<< "Vulkan backend requested but dynamic render backend is not enabled. Falling back to OpenGL.";
		backend_ = kOpenGL;
#endif
	}

	if (backend_ == k_open_gl || backend_ == k_vulkan) {
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		auto *dynamic_renderer =
			new DynamicRenderer(backend_to_string(requested_backend_));
		if (!dynamic_renderer->load()) {
			qWarning() << "Failed to load dynamic render backend"
					   << backend_to_string(requested_backend_)
					   << ", falling back to OpenGL";
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
				qWarning() << "Dynamic render backend fell back from"
						   << backend_to_string(backend_) << "to"
						   << backend_to_string(actual_backend);
				backend_ = actual_backend;
			}
		}
#else
		context_ = new OpenGLRenderer();
#endif
		decoder_cache_ = new DecoderCache();
		shader_cache_ = new ShaderCache();
	} else {
		qCritical() << "Tried to initialize unknown graphics backend";
		context_ = nullptr;
		decoder_cache_ = nullptr;
	}

	if (context_) {
		dry_run_thread_ = create_thread();
		audio_thread_ = create_thread();

		waveform_threads_.resize(QThread::idealThreadCount());
		for (size_t i = 0; i < waveform_threads_.size(); i++) {
			waveform_threads_[i] = create_thread();
		}

		auto_cacher_ = new PreviewAutoCacher(this);

		worker_pool_ = new RenderWorkerPool(
			decoder_cache_, backend_to_string(requested_backend_), this);
		worker_pool_->start(QThread::NormalPriority);
		backend_ = k_multi_process;
	}

	decoder_clear_timer_ = new QTimer(this);
	decoder_clear_timer_->setInterval(k_decoder_maximum_inactivity);
	connect(decoder_clear_timer_, &QTimer::timeout, this,
			&RenderManager::clear_old_decoders);
	decoder_clear_timer_->start();
}

RenderManager::~RenderManager()
{
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
		}

		context_->post_destroy();
		delete context_;
	}
}

RenderThread *RenderManager::create_thread(Renderer *renderer)
{
	auto t = new RenderThread(renderer, decoder_cache_, shader_cache_, this);
	render_threads_.push_back(t);
	t->start(QThread::NormalPriority);
	return t;
}

RenderTicketPtr RenderManager::render_frame(const RenderVideoParams &params)
{
	// Create ticket
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();

	ticket->setProperty("node", QtUtils::ptr_to_value(params.node));
	ticket->setProperty("time", QVariant::fromValue(params.time));
	ticket->setProperty("size", params.force_size);
	ticket->setProperty("matrix", params.force_matrix);
	ticket->setProperty("format",
						static_cast<PixelFormat::Format>(params.force_format));
	ticket->setProperty("usecache", params.use_cache);
	ticket->setProperty("channelcount", params.force_channel_count);
	ticket->setProperty("mode", params.mode);
	ticket->setProperty("type", k_type_video);
	ticket->setProperty("colormanager",
						QtUtils::ptr_to_value(params.color_manager));
	ticket->setProperty("coloroutput",
						QVariant::fromValue(params.force_color_output));
	ticket->setProperty("colortransform",
						QVariant::fromValue(params.force_color_transform));
	Q_ASSERT(params.video_params.is_valid());
	ticket->setProperty("vparam", QVariant::fromValue(params.video_params));
	ticket->setProperty("aparam", QVariant::fromValue(params.audio_params));
	ticket->setProperty("return", params.return_type);
	ticket->setProperty("cache", params.cache_dir);
	ticket->setProperty("cachetimebase",
						QVariant::fromValue(params.cache_timebase));
	ticket->setProperty("cacheid", QVariant::fromValue(params.cache_id));
	ticket->setProperty("multicam", QtUtils::ptr_to_value(params.multicam));

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
		qWarning()
			<< "RenderManager: worker pool unavailable, finishing ticket "
			   "without result";
		ticket->finish();
	}

	return ticket;
}

RenderTicketPtr RenderManager::render_audio(const RenderAudioParams &params)
{
	// Create ticket
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();

	ticket->setProperty("node", QtUtils::ptr_to_value(params.node));
	ticket->setProperty("time", QVariant::fromValue(params.range));
	ticket->setProperty("type", k_type_audio);
	ticket->setProperty("enablewaveforms", params.generate_waveforms);
	ticket->setProperty("clamp", params.clamp);
	ticket->setProperty("aparam", QVariant::fromValue(params.audio_params));
	ticket->setProperty("mode", params.mode);

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
		decoder_clear_timer_->setInterval(k_decoder_maximum_inactivity_aggressive);
	} else {
		decoder_clear_timer_->setInterval(k_decoder_maximum_inactivity);
	}
}

void RenderManager::clear_old_decoders()
{
	if (!decoder_cache_) {
		// No decoder cache exists on backends without a renderer (e.g. dummy)
		return;
	}

	QMutexLocker locker(decoder_cache_->mutex());

	qint64 min_age =
		QDateTime::currentMSecsSinceEpoch() - k_decoder_maximum_inactivity;

	for (auto it = decoder_cache_->begin(); it != decoder_cache_->end();) {
		DecoderPair decoder = it.value();

		if (decoder.decoder->get_last_accessed_time() < min_age) {
			decoder.decoder->close();
			it = decoder_cache_->erase(it);
		} else {
			it++;
		}
	}
}

RenderThread::RenderThread(Renderer *renderer, DecoderCache *decoder_cache,
						   ShaderCache *shader_cache, QObject *parent)
	: QThread(parent)
	, cancelled_(false)
	, context_(renderer)
	, decoder_cache_(decoder_cache)
	, shader_cache_(shader_cache)
{
	if (context_) {
		context_->init();
		context_->moveToThread(this);
	}
}

void RenderThread::add_ticket(RenderTicketPtr ticket)
{
	QMutexLocker locker(&mutex_);
	ticket->moveToThread(this);
	queue_.push_back(ticket);
	wait_.wakeOne();
}

bool RenderThread::remove_ticket(RenderTicketPtr ticket)
{
	QMutexLocker locker(&mutex_);

	auto it = std::find(queue_.begin(), queue_.end(), ticket);
	if (it == queue_.end()) {
		return false;
	}

	queue_.erase(it);
	return true;
}

void RenderThread::quit()
{
	QMutexLocker locker(&mutex_);
	cancelled_ = true;
	wait_.wakeOne();
}

void RenderThread::run()
{
	if (context_) {
		context_->post_init();
	}

	QMutexLocker locker(&mutex_);

	while (!cancelled_) {
		if (queue_.empty()) {
			wait_.wait(&mutex_);
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

			locker.relock();
		}
	}

	if (context_) {
		context_->destroy();
		context_->moveToThread(this->thread());
	}
}

}
