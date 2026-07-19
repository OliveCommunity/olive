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

#ifndef OAK_RENDERBACKEND_H
#define OAK_RENDERBACKEND_H

#include <QtConcurrent/QtConcurrent>

#include "config/config.h"
#include "colorprocessorcache.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/traverser.h"
#include "render/previewautocacher.h"
#include "render/renderer.h"
#include "render/colortransform.h"
#include "render/renderticket.h"
#include "rendercache.h"

namespace olive
{

class RenderThread : public QThread {
	Q_OBJECT
public:
	RenderThread(Renderer *renderer, DecoderCache *decoder_cache,
				 ShaderCache *shader_cache, QObject *parent = nullptr);

	void add_ticket(RenderTicketPtr ticket);

	bool remove_ticket(RenderTicketPtr ticket);

	void quit();

protected:
	virtual void run() override;

private:
	QMutex mutex_;

	QWaitCondition wait_;

	std::list<RenderTicketPtr> queue_;

	bool cancelled_;

	Renderer *context_ = nullptr;

	DecoderCache *decoder_cache_ = nullptr;

	ShaderCache *shader_cache_ = nullptr;
};

class RenderWorkerPool;

class RenderManager : public QObject {
	Q_OBJECT
public:
	enum Backend {
		/// Graphics acceleration provided by OpenGL
		k_open_gl,

		/// Vulkan requested by the user. Falls back to OpenGL until VulkanRenderer is implemented.
		k_vulkan,

		/// Video frames are rendered by an external oak-render-worker process.
		k_multi_process,

		/// No graphics rendering - used to test core threading logic
		k_dummy
	};

	static void create_instance()
	{
		instance_ = new RenderManager();
	}

	static void destroy_instance()
	{
		delete instance_;
		instance_ = nullptr;
	}

	static RenderManager *instance()
	{
		return instance_;
	}

	enum ReturnType { k_texture, k_frame, k_null };

	struct RenderVideoParams {
		RenderVideoParams(Node *n, const VideoParams &vparam,
						  const AudioParams &aparam, const Rational &t,
						  ColorManager *colorman, RenderMode::Mode m)
		{
			node = n;
			video_params = vparam;
			audio_params = aparam;
			time = t;
			color_manager = colorman;
			use_cache = false;
			return_type = k_frame;
			force_format = PixelFormat::invalid;
			force_color_output = nullptr;
			force_color_transform = ColorTransform();
			force_size = QSize(0, 0);
			force_channel_count = 0;
			mode = m;
			multicam = nullptr;
		}

		void add_cache(FrameHashCache *cache)
		{
			cache_dir = cache->get_cache_directory();
			cache_timebase = cache->get_timebase();
			cache_id = cache->get_uuid().toString();
		}

		Node *node;
		VideoParams video_params;
		AudioParams audio_params;
		Rational time;
		ColorManager *color_manager;
		bool use_cache;
		ReturnType return_type;
		RenderMode::Mode mode;
		MultiCamNode *multicam;

		QString cache_dir;
		Rational cache_timebase;
		QString cache_id;

		QSize force_size;
		int force_channel_count;
		QMatrix4x4 force_matrix;
		PixelFormat force_format;
		ColorProcessorPtr force_color_output;
		ColorTransform force_color_transform;
	};

	static const Rational k_dry_run_interval;

	/**
   * @brief Asynchronously generate a frame at a given time
   *
   * The ticket from this function will return a FramePtr - the rendered frame in reference color
   * space.
   *
   * This function is thread-safe.
   */
	RenderTicketPtr render_frame(const RenderVideoParams &params);

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
	RenderTicketPtr render_audio(const RenderAudioParams &params);

	bool remove_ticket(RenderTicketPtr ticket);

	enum TicketType { k_type_video, k_type_audio };

	Backend backend() const
	{
		return backend_;
	}

	Backend requested_backend() const
	{
		return requested_backend_;
	}

	static Backend backend_from_string(const QString &backend);
	static QString backend_to_string(Backend backend);

	PreviewAutoCacher *get_cacher() const
	{
		return auto_cacher_;
	}

	void set_project(Project *p)
	{
		auto_cacher_->set_project(p);
	}

public slots:
	void set_aggressive_garbage_collection(bool enabled);

signals:

private:
	RenderManager(QObject *parent = nullptr);

	virtual ~RenderManager() override;

	RenderThread *create_thread(Renderer *renderer = nullptr);

	static RenderManager *instance_;

	Renderer *context_ = nullptr;

	Backend backend_;
	Backend requested_backend_;

	DecoderCache *decoder_cache_ = nullptr;

	ShaderCache *shader_cache_ = nullptr;

	static constexpr auto k_decoder_maximum_inactivity_aggressive = 1000;
	static constexpr auto k_decoder_maximum_inactivity = 5000;

	int aggressive_gc_ = 0;

	QTimer *decoder_clear_timer_ = nullptr;

	RenderThread *dry_run_thread_ = nullptr;
	RenderThread *audio_thread_ = nullptr;

	std::vector<RenderThread *> waveform_threads_;
	size_t last_waveform_thread_ = 0;

	std::list<RenderThread *> render_threads_;

	PreviewAutoCacher *auto_cacher_ = nullptr;

	RenderWorkerPool *worker_pool_ = nullptr;

private slots:
	void clear_old_decoders();
};

}

Q_DECLARE_METATYPE(olive::RenderManager::TicketType)

#endif // OAK_RENDERBACKEND_H
