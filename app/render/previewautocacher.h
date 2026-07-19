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

#ifndef OAK_AUTOCACHER_H
#define OAK_AUTOCACHER_H

#include <QtConcurrent/QtConcurrent>

#include "config/config.h"
#include "node/color/colormanager/colormanager.h"
#include "node/group/group.h"
#include "node/node.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "render/projectcopier.h"
#include "render/renderjobtracker.h"
#include "render/renderticket.h"

namespace olive
{

/**
 * @brief Manager for dynamically caching a sequence in the background
 *
 * Intended to be used with a Viewer to dynamically cache parts of a sequence based on the playhead.
 */
class PreviewAutoCacher : public QObject {
	Q_OBJECT
public:
	PreviewAutoCacher(QObject *parent = nullptr);

	virtual ~PreviewAutoCacher() override;

	RenderTicketPtr get_single_frame(ViewerOutput *viewer, const Rational &t,
								   bool dry = false);
	RenderTicketPtr get_single_frame(Node *n, ViewerOutput *viewer,
								   const Rational &t, bool dry = false);

	RenderTicketPtr get_range_of_audio(ViewerOutput *viewer, TimeRange range);

	void clear_single_frame_renders();
	void clear_single_frame_renders_that_arent_running();

	/**
   * @brief Set the viewer node to auto-cache
   */
	void set_project(Project *project);

	/**
   * @brief Force a certain range to be cached
   *
   * Usually, PreviewAutoCacher caches a user-defined range around the playhead, however there are
   * times they may want certain non-playhead-related time ranges to be cached (i.e. entire sequence
   * or in/out range), so that can be set here.
   */
	void force_cache_range(ViewerOutput *context, const TimeRange &range);

	/**
   * @brief Updates the range of frames to auto-cache
   */
	void set_playhead(const Rational &playhead);

	/**
   * @brief Call cancel on all currently running video tasks
   *
   * Signalling cancel to a video task indicates that we're no longer interested in its end result.
   * This does not end all video tasks immediately, the RenderManager will do what it can to speed
   * up finishing the task. The RenderManager will  also return "no result", which can be checked
   * with watcher->HasResult.
   */
	void cancel_video_tasks(bool and_wait_for_them_to_finish = false);
	void cancel_audio_tasks(bool and_wait_for_them_to_finish = false);

	bool is_rendering_custom_range() const;

	void set_renders_paused(bool e);
	void set_thumbnails_paused(bool e);

	void set_multicam_node(MultiCamNode *n)
	{
		multicam_ = n;
	}

	void set_ignore_cache_requests(bool e)
	{
		ignore_cache_requests_ = e;
	}

public slots:
	void set_display_color_processor(ColorProcessorPtr processor)
	{
		display_color_processor_ = processor;
	}

signals:
	void stop_cache_proxy_tasks();

	void signal_cache_proxy_task_progress(double d);

private:
	void try_render();

	RenderTicketWatcher *render_frame(Node *node, ViewerOutput *context,
									 const Rational &time, PlaybackCache *cache,
									 bool dry);

	RenderTicketPtr render_audio(Node *node, ViewerOutput *context,
								const TimeRange &range, PlaybackCache *cache);

	void connect_to_node_cache(Node *node);
	void disconnect_from_node_cache(Node *node);

	void cancel_queued_single_frame_render();

	void start_caching_range(const TimeRange &range, TimeRangeList *range_list,
						   RenderJobTracker *tracker);
	void start_caching_video_range(ViewerOutput *context, PlaybackCache *cache,
								const TimeRange &range);
	void start_caching_audio_range(ViewerOutput *context, PlaybackCache *cache,
								const TimeRange &range);

	void video_invalidated_from_node(ViewerOutput *context, PlaybackCache *cache,
								  const olive::TimeRange &range);
	void audio_invalidated_from_node(ViewerOutput *context, PlaybackCache *cache,
								  const olive::TimeRange &range);

	Project *project_;

	ProjectCopier *copier_;

	TimeRange cache_range_;

	bool use_custom_range_;
	TimeRange custom_autocache_range_;

	bool pause_renders_;
	bool pause_thumbnails_;

	RenderTicketPtr single_frame_render_;
	QMap<RenderTicketWatcher *, QVector<RenderTicketPtr>>
		video_immediate_passthroughs_;

	QTimer delayed_requeue_timer_;

	JobTime last_conform_task_;

	QVector<RenderTicketWatcher *> running_video_tasks_;
	QVector<RenderTicketWatcher *> running_audio_tasks_;

	ColorManager *copied_color_manager_;

	struct VideoJob {
		Node *node;
		ViewerOutput *context;
		PlaybackCache *cache;
		TimeRange range;
		TimeRangeListFrameIterator iterator;
	};

	struct VideoCacheData {
		RenderJobTracker job_tracker;
	};

	struct AudioJob {
		Node *node;
		ViewerOutput *context;
		PlaybackCache *cache;
		TimeRange range;
	};

	struct AudioCacheData {
		RenderJobTracker job_tracker;
		TimeRangeList needs_conform;
		ViewerOutput *context = nullptr;
	};

	std::list<VideoJob> pending_video_jobs_;
	std::list<AudioJob> pending_audio_jobs_;

	QHash<PlaybackCache *, VideoCacheData> video_cache_data_;
	QHash<PlaybackCache *, AudioCacheData> audio_cache_data_;

	ColorProcessorPtr display_color_processor_;

	MultiCamNode *multicam_;

	bool ignore_cache_requests_;

private slots:
	/**
   * @brief Handler for when the NodeGraph reports a video change over a certain time range
   */
	void video_invalidated_from_cache(ViewerOutput *context,
								   const olive::TimeRange &range);

	/**
   * @brief Handler for when the NodeGraph reports a audio change over a certain time range
   */
	void audio_invalidated_from_cache(ViewerOutput *context,
								   const olive::TimeRange &range);

	void cancel_for_cache();

	/**
   * @brief Handler for when the RenderManager has returned rendered audio
   */
	void audio_rendered();

	/**
   * @brief Handler for when the RenderManager has returned rendered video frames
   */
	void video_rendered();

	/**
   * @brief Generic function called whenever the frames to render need to be (re)queued
   */
	//void RequeueFrames();

	void conform_finished();

	void cache_proxy_task_cancelled();
};

}

#endif // OAK_AUTOCACHER_H
