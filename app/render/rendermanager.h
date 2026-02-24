/***
  This file is part of Oak Video Editor - A fork of original project Olive 

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

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

#ifndef RENDERBACKEND_H
#define RENDERBACKEND_H

#include <QtConcurrent/QtConcurrent>

#include "config/config.h"
#include "colorprocessorcache.h"
#include "dialog/rendercancel/rendercancel.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/traverser.h"
#include "render/opengl/openglthread.h"
#include "render/previewautocacher.h"
#include "render/renderer.h"
#include "render/renderticket.h"
#include "rendercache.h"

#include <deque>

namespace olive
{

enum class RenderPriority {
	kPlayback = 0,
	kPreview = 1,
	kCache = 2,
	kExport = 3
};

// 前向声明
class RenderThread;

// 任务队列接口，用于工作窃取
class WorkStealingQueue {
public:
	virtual ~WorkStealingQueue() = default;

	// 尝试从队列尾部弹出任务（供拥有者线程使用）
	virtual bool TryPopBack(RenderTicketPtr &ticket) = 0;

	// 尝试从队列头部窃取任务（供其他线程使用）
	virtual bool TrySteal(RenderTicketPtr &ticket) = 0;

	// 添加任务到队列尾部
	virtual void PushBack(RenderTicketPtr ticket) = 0;

	// 获取队列大小
	virtual size_t Size() const = 0;

	// 是否为空
	virtual bool Empty() const = 0;
};

/**
 * @brief 渲染线程 - 仅执行 CPU 任务
 * 
 * 所有 OpenGL 操作通过 gl_thread_ 提交到专门的 OpenGL 线程执行
 */
class RenderThread : public QThread {
	Q_OBJECT
public:
	RenderThread(DecoderCache *decoder_cache, ShaderCache *shader_cache,
				 OpenGLThread *gl_thread, QObject *parent = nullptr);

	void AddTicket(RenderTicketPtr ticket);
	
	// 工作窃取：尝试从本线程窃取任务
	bool TrySteal(RenderTicketPtr &ticket);

	// 获取队列大小（线程安全）
	size_t QueueSize() const;

	bool RemoveTicket(RenderTicketPtr ticket);

	// 取消队列中的所有任务
	void CancelAllTickets();

	void quit();

	OpenGLThread *GetGLThread() const { return gl_thread_; }

protected:
	virtual void run() override;

private:
	// 尝试从其他线程窃取任务
	RenderTicketPtr StealFromOthers();

	// 获取所有线程列表（用于工作窃取）
	static std::vector<RenderThread *> &GetAllThreads();

	// 注册/注销线程
	void RegisterThread();
	void UnregisterThread();

	QMutex mutex_;

	QWaitCondition wait_;

	std::deque<RenderTicketPtr> queue_;  // 本地任务队列

	bool cancelled_;

	DecoderCache *decoder_cache_;

	ShaderCache *shader_cache_;

	OpenGLThread *gl_thread_;  // 共享的 OpenGL 线程
	
	// 用于随机窃取的起始索引，避免所有线程都窃取同一个
	size_t steal_start_index_ = 0;

	// 静态成员，存储所有渲染线程
	static QMutex s_all_threads_mutex_;
	static std::vector<RenderThread *> s_all_threads_;
	static thread_local RenderThread *s_current_thread_;
};

typedef std::shared_ptr<RenderThread> RenderThreadPtr;

class RenderManager : public QObject {
	Q_OBJECT
public:
	enum Backend {
		/// Graphics acceleration provided by OpenGL
		kOpenGL,

		/// No graphics rendering - used to test core threading logic
		kDummy
	};

	static void CreateInstance()
	{
		instance_ = new RenderManager();
	}

	/**
	 * @brief 创建测试实例（使用 kDummy backend，不需要 GPU）
	 */
	static void CreateTestInstance()
	{
		instance_ = new RenderManager(kDummy);
	}

	static void DestroyInstance()
	{
		delete instance_;
		instance_ = nullptr;
	}

	static RenderManager *instance()
	{
		return instance_;
	}

	enum ReturnType { kTexture, kFrame, kNull };

	struct RenderVideoParams {
		RenderVideoParams(Node *n, const VideoParams &vparam,
						  const AudioParams &aparam, const rational &t,
						  ColorManager *colorman, RenderMode::Mode m,
						  RenderPriority prio)
		{
			node = n;
			video_params = vparam;
			audio_params = aparam;
			time = t;
			color_manager = colorman;
			use_cache = false;
			return_type = kFrame;
			force_format = PixelFormat::INVALID;
			force_color_output = nullptr;
			force_size = QSize(0, 0);
			force_channel_count = 0;
			mode = m;
			multicam = nullptr;
			priority = prio;
		}

		void AddCache(FrameHashCache *cache)
		{
			cache_dir = cache->GetCacheDirectory();
			cache_timebase = cache->GetTimebase();
			cache_id = cache->GetUuid().toString();
		}

		Node *node;
		VideoParams video_params;
		AudioParams audio_params;
		rational time;
		ColorManager *color_manager;
		bool use_cache;
		ReturnType return_type;
		RenderMode::Mode mode;
		MultiCamNode *multicam;

		QString cache_dir;
		rational cache_timebase;
		QString cache_id;

		QSize force_size;
		int force_channel_count;
		QMatrix4x4 force_matrix;
		PixelFormat force_format;
		ColorProcessorPtr force_color_output;
		RenderPriority priority;
	};

	static const rational kDryRunInterval;

	/**
	   * @brief Asynchronously generate a frame at a given time
	   *
	   * The ticket from this function will return a FramePtr - the rendered frame in reference color
	   * space.
	   *
	   * This function is thread-safe.
	   */
	RenderTicketPtr RenderFrame(const RenderVideoParams &params);

	struct RenderAudioParams {
		RenderAudioParams(Node *n, const TimeRange &time,
						  const AudioParams &aparam, RenderMode::Mode m)
		{
			node = n;
			range = time;
			audio_params = aparam;
			generate_waveforms = false;
			clamp = true;
			mode = m;
		}

		Node *node;
		TimeRange range;
		AudioParams audio_params;
		bool generate_waveforms;
		bool clamp;
		RenderMode::Mode mode;
	};

	/**
	   * @brief Asynchronously generate a chunk of audio
	   *
	   * The ticket from this function will return a SampleBufferPtr - the rendered audio.
	   *
	   * This function is thread-safe.
	   */
	RenderTicketPtr RenderAudio(const RenderAudioParams &params);

	bool RemoveTicket(RenderTicketPtr ticket);

	enum TicketType { kTypeVideo, kTypeAudio };

	Backend backend() const
	{
		return backend_;
	}

	PreviewAutoCacher *GetCacher() const
	{
		return auto_cacher_;
	}

	void SetProject(Project *p)
	{
		auto_cacher_->SetProject(p);
	}

	/**
	 * @brief 获取 OpenGL 线程（单线程）
	 */
	OpenGLThread *GetGLThread() const { return gl_thread_; }

	DecoderCache *GetDecoderCache() const { return decoder_cache_; }
	ShaderCache *GetShaderCache() const { return shader_cache_; }

	/**
	 * @brief 获取视频渲染线程数量
	 */
	int GetVideoThreadCount() const { return video_threads_.size(); }

public slots:
	void SetAggressiveGarbageCollection(bool enabled);

signals:

private:
	explicit RenderManager(Backend backend = kOpenGL, QObject *parent = nullptr);

	virtual ~RenderManager() override;

	RenderThread *CreateThread();

	static RenderManager *instance_;

	Backend backend_;

	DecoderCache *decoder_cache_;

	ShaderCache *shader_cache_;

	// 单线程 OpenGL 线程
	OpenGLThread *gl_thread_;

	static constexpr auto kDecoderMaximumInactivityAggressive = 1000;
	static constexpr auto kDecoderMaximumInactivity = 5000;

	int aggressive_gc_;

	QTimer *decoder_clear_timer_;

	QList<RenderThreadPtr> video_threads_;
	RenderThread *dry_run_thread_;
	RenderThread *audio_thread_;

	std::vector<RenderThread *> waveform_threads_;
	size_t last_waveform_thread_;

	std::list<RenderThread *> render_threads_;

	PreviewAutoCacher *auto_cacher_;

	std::shared_ptr<QThread> scheduler_thread_;
	
	// 用于选择负载最轻的线程
	RenderThread* SelectBestThread();

	// 获取负载最轻的线程索引
	size_t GetLightestThreadIndex();
private slots:
	void ClearOldDecoders();
};

}

Q_DECLARE_METATYPE(olive::RenderManager::TicketType)

#endif // RENDERBACKEND_H
