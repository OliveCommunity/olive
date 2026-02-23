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
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>
#include <atomic>

#include "config/config.h"
#include "core.h"
#include "render/renderprocessor.h"
#include "task/conform/conform.h"
#include "task/taskmanager.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

RenderManager *RenderManager::instance_ = nullptr;
const rational RenderManager::kDryRunInterval = rational(10);

// 静态成员初始化
QMutex RenderThread::s_all_threads_mutex_;
std::vector<RenderThread *> RenderThread::s_all_threads_;
thread_local RenderThread *RenderThread::s_current_thread_ = nullptr;

RenderManager::RenderManager(Backend backend, QObject *parent)
	: backend_(backend)
	, gl_thread_(nullptr)
	, aggressive_gc_(0)
{
	if (backend_ == kOpenGL) {
		// 创建单线程 OpenGL 线程
		gl_thread_ = new OpenGLThread(this);
		gl_thread_->start(QThread::HighPriority);

		decoder_cache_ = new DecoderCache();
		shader_cache_ = new ShaderCache();
	} else if (backend_ == kDummy) {
		// 测试模式：不创建 OpenGL 线程
		gl_thread_ = nullptr;
		decoder_cache_ = nullptr;
		shader_cache_ = nullptr;
	} else {
		qCritical() << "Tried to initialize unknown graphics backend";
		gl_thread_ = nullptr;
		decoder_cache_ = nullptr;
		shader_cache_ = nullptr;
	}

	if (gl_thread_ || backend_ == kDummy) {
		int num_of_threads = QThread::idealThreadCount();
		if (num_of_threads <= 0) {
			num_of_threads = 6;
		}

		// 创建工作窃取线程池，但保留至少2个线程给系统
		int video_thread_count = std::max(1, num_of_threads - 2);
		for (int i = 0; i < video_thread_count; i++) {
			video_threads_.append(
				std::shared_ptr<RenderThread>(CreateThread()));
		}

		dry_run_thread_ = CreateThread();
		audio_thread_ = CreateThread();

		waveform_threads_.resize(QThread::idealThreadCount());
		for (size_t i = 0; i < waveform_threads_.size(); i++) {
			waveform_threads_[i] = CreateThread();
		}

		auto_cacher_ = new PreviewAutoCacher(this);
	}

	decoder_clear_timer_ = new QTimer(this);
	decoder_clear_timer_->setInterval(kDecoderMaximumInactivity);
	connect(decoder_clear_timer_, &QTimer::timeout, this,
			&RenderManager::ClearOldDecoders);
	decoder_clear_timer_->start();
}

RenderManager::~RenderManager()
{
	if (gl_thread_) {
		// 停止所有渲染线程
		for (RenderThread *rt : render_threads_) {
			rt->quit();
			rt->wait();
		}

		// 停止 OpenGL 线程
		gl_thread_->Stop();
		gl_thread_->wait();
		delete gl_thread_;

		delete shader_cache_;
		delete decoder_cache_;
	}
}

RenderThread *RenderManager::CreateThread()
{
	auto t = new RenderThread(decoder_cache_, shader_cache_, gl_thread_, this);
	render_threads_.push_back(t);
	t->start(QThread::NormalPriority);
	return t;
}

RenderThread *RenderManager::SelectBestThread()
{
	// 使用轮询策略均匀分配任务，避免任务堆积在同一个线程
	static std::atomic<size_t> next_thread_index{ 0 };
	size_t index = next_thread_index++ % video_threads_.size();
	return video_threads_[index].get();
}

size_t RenderManager::GetLightestThreadIndex()
{
	size_t best_index = 0;
	size_t min_size = SIZE_MAX;

	for (size_t i = 0; i < video_threads_.size(); ++i) {
		size_t size = video_threads_[i]->QueueSize();
		if (size < min_size) {
			min_size = size;
			best_index = i;
		}
	}

	return best_index;
}

RenderTicketPtr RenderManager::RenderFrame(const RenderVideoParams &params)
{
	qDebug() << "RenderManager::RenderFrame: return_type=" << params.return_type 
			 << "(kNull=" << ReturnType::kNull << ", kTexture=" << ReturnType::kTexture << ", kFrame=" << ReturnType::kFrame << ")";

	// Create ticket
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();

	ticket->setProperty("node", QtUtils::PtrToValue(params.node));
	ticket->setProperty("time", QVariant::fromValue(params.time));
	ticket->setProperty("size", params.force_size);
	ticket->setProperty("matrix", params.force_matrix);
	ticket->setProperty("format",
						static_cast<PixelFormat::Format>(params.force_format));
	ticket->setProperty("usecache", params.use_cache);
	ticket->setProperty("channelcount", params.force_channel_count);
	ticket->setProperty("mode", params.mode);
	ticket->setProperty("type", kTypeVideo);
	ticket->setProperty("colormanager",
						QtUtils::PtrToValue(params.color_manager));
	ticket->setProperty("coloroutput",
						QVariant::fromValue(params.force_color_output));
	Q_ASSERT(params.video_params.is_valid());
	ticket->setProperty("vparam", QVariant::fromValue(params.video_params));
	ticket->setProperty("aparam", QVariant::fromValue(params.audio_params));
	ticket->setProperty("return", params.return_type);
	ticket->setProperty("cache", params.cache_dir);
	ticket->setProperty("cachetimebase",
						QVariant::fromValue(params.cache_timebase));
	ticket->setProperty("cacheid", QVariant::fromValue(params.cache_id));
	ticket->setProperty("multicam", QtUtils::PtrToValue(params.multicam));

	ticket->setProperty("priority", QVariant::fromValue(params.priority));
	if (params.return_type == ReturnType::kNull) {
		qDebug() << "RenderManager::RenderFrame: Using dry_run_thread";
		dry_run_thread_->AddTicket(ticket);
	} else {
		// 使用工作窃取策略：选择当前最空闲的线程
		RenderThread *thread = SelectBestThread();
		qDebug() << "RenderManager::RenderFrame: Using video thread" << thread;
		thread->AddTicket(ticket);
	}

	return ticket;
}

RenderTicketPtr RenderManager::RenderAudio(const RenderAudioParams &params)
{
	// Create ticket
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();

	ticket->setProperty("node", QtUtils::PtrToValue(params.node));
	ticket->setProperty("time", QVariant::fromValue(params.range));
	ticket->setProperty("type", kTypeAudio);
	ticket->setProperty("enablewaveforms", params.generate_waveforms);
	ticket->setProperty("clamp", params.clamp);
	ticket->setProperty("aparam", QVariant::fromValue(params.audio_params));
	ticket->setProperty("mode", params.mode);

	if (params.generate_waveforms) {
		size_t thread_index = last_waveform_thread_ % waveform_threads_.size();
		RenderThread *thread = waveform_threads_[thread_index];
		thread->AddTicket(ticket);
		last_waveform_thread_++;
	} else {
		audio_thread_->AddTicket(ticket);
	}

	return ticket;
}

bool RenderManager::RemoveTicket(RenderTicketPtr ticket)
{
	for (RenderThread *rt : render_threads_) {
		if (rt->RemoveTicket(ticket)) {
			return true;
		}
	}

	return false;
}

void RenderManager::SetAggressiveGarbageCollection(bool enabled)
{
	aggressive_gc_ += enabled ? 1 : -1;

	if (aggressive_gc_ > 0) {
		decoder_clear_timer_->setInterval(kDecoderMaximumInactivityAggressive);
	} else {
		decoder_clear_timer_->setInterval(kDecoderMaximumInactivity);
	}
}

void RenderManager::ClearOldDecoders()
{
	QMutexLocker locker(decoder_cache_->mutex());

	qint64 min_age =
		QDateTime::currentMSecsSinceEpoch() - kDecoderMaximumInactivity;

	for (auto it = decoder_cache_->begin(); it != decoder_cache_->end();) {
		DecoderPair decoder = it.value();

		if (decoder.decoder->GetLastAccessedTime() < min_age) {
			decoder.decoder->Close();
			it = decoder_cache_->erase(it);
		} else {
			it++;
		}
	}
}

RenderThread::RenderThread(DecoderCache *decoder_cache,
						   ShaderCache *shader_cache,
						   OpenGLThread *gl_thread,
						   QObject *parent)
	: QThread(parent)
	, cancelled_(false)
	, decoder_cache_(decoder_cache)
	, shader_cache_(shader_cache)
	, gl_thread_(gl_thread)
	, steal_start_index_(0)
{
}

void RenderThread::RegisterThread()
{
	QMutexLocker locker(&s_all_threads_mutex_);
	s_all_threads_.push_back(this);
	s_current_thread_ = this;
}

void RenderThread::UnregisterThread()
{
	QMutexLocker locker(&s_all_threads_mutex_);
	auto it = std::find(s_all_threads_.begin(), s_all_threads_.end(), this);
	if (it != s_all_threads_.end()) {
		s_all_threads_.erase(it);
	}
}

std::vector<RenderThread *> &RenderThread::GetAllThreads()
{
	return s_all_threads_;
}

void RenderThread::AddTicket(RenderTicketPtr ticket)
{
	QMutexLocker locker(&mutex_);
	queue_.push_back(ticket);
	wait_.wakeOne();
}

bool RenderThread::TrySteal(RenderTicketPtr &ticket)
{
	QMutexLocker locker(&mutex_);

	// 只能从队列头部窃取（保持任务顺序）
	if (!queue_.empty()) {
		ticket = queue_.front();
		queue_.pop_front();
		return true;
	}

	return false;
}

size_t RenderThread::QueueSize() const
{
	QMutexLocker locker(const_cast<QMutex *>(&mutex_));
	return queue_.size();
}

bool RenderThread::RemoveTicket(RenderTicketPtr ticket)
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

RenderTicketPtr RenderThread::StealFromOthers()
{
	// 快速路径：如果没有其他线程，直接返回
	if (s_all_threads_.size() <= 1) {
		return nullptr;
	}

	// 从随机位置开始窃取，避免所有线程竞争同一个队列
	size_t num_threads = s_all_threads_.size();
	size_t start_idx = steal_start_index_++ % num_threads;

	// 尝试从其他线程窃取
	for (size_t i = 0; i < num_threads; ++i) {
		size_t idx = (start_idx + i) % num_threads;
		RenderThread *other = s_all_threads_[idx];

		// 跳过自己
		if (other == this) {
			continue;
		}

		RenderTicketPtr stolen_ticket;
		if (other->TrySteal(stolen_ticket)) {
			return stolen_ticket;
		}
	}

	return nullptr;
}

void RenderThread::run()
{
	// 注册到全局列表
	RegisterThread();

	QMutexLocker locker(&mutex_);

	while (!cancelled_) {
		RenderTicketPtr ticket;
		bool have_task = false;

		// 1. 首先尝试从自己的队列取任务（从尾部取，LIFO - 更好的缓存局部性）
		if (!queue_.empty()) {
			ticket = queue_.back();
			queue_.pop_back();
			have_task = true;
		}

		if (!have_task) {
			// 2. 自己的队列为空，先解锁
			locker.unlock();

			// 3. 尝试从其他线程窃取任务
			ticket = StealFromOthers();

			if (ticket) {
				have_task = true;
			} else {
				// 4. 窃取失败，重新加锁并等待
				locker.relock();

				// 双重检查：等待前再检查一次队列
				if (!queue_.empty()) {
					ticket = queue_.back();
					queue_.pop_back();
					have_task = true;
				} else if (!cancelled_) {
					// 确实没有任务，等待新任务
					wait_.wait(&mutex_);
					continue; // 回到循环开始，重新检查
				}
			}

			if (have_task) {
				// 重新加锁以保持锁状态一致
				locker.relock();
			}
		}

		if (!have_task) {
			continue;
		}

		// 解锁以执行渲染任务
		locker.unlock();

		// Setup the ticket for ::Process
		ticket->Start();

		if (ticket->IsCancelled()) {
			ticket->Finish();
		} else {
			// 使用 OpenGL 线程执行渲染
			RenderProcessor::Process(ticket, gl_thread_, decoder_cache_,
									 shader_cache_);
		}

		// 重新加锁继续循环
		locker.relock();
	}

	// 注销
	UnregisterThread();
}

} // namespace olive
