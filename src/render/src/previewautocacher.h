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

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include "color/colormanager/colormanager.h"
#include "configaccessor.h"
#include "group/group.h"
#include "node.h"
#include "output/viewer/viewer.h"
#include "project.h"
#include "projectcopier.h"
#include "renderjobtracker.h"
#include "renderticket.h"

namespace olive
{

class MultiCamNode;

/**
 * @brief Manager for dynamically caching a sequence in the background
 *
 * Intended to be used with a Viewer to dynamically cache parts of a sequence based on the playhead.
 *
 * De-Qt notes:
 *  - No longer a QObject. The ProjectCopier is owned via std::unique_ptr.
 *  - The former signals (stop_cache_proxy_tasks, signal_cache_proxy_task_progress)
 *    are gone; the facade re-emits progress via oakengine_event if needed.
 *  - The former single-shot QTimer requeue delay is replaced by an explicit
 *    pending flag: when a node copy is not ready yet, try_render() sets
 *    delayed_requeue_pending_ and the facade is expected to call try_render()
 *    again after requeue_delay_ms() milliseconds (single-threaded semantics
 *    preserved; no worker thread calls into the graph).
 */
class PreviewAutoCacher {
public:
	PreviewAutoCacher();

	virtual ~PreviewAutoCacher();

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

	void set_display_color_processor(ColorProcessorPtr processor)
	{
		display_color_processor_ = processor;
	}

	/**
   * @brief Progress/throttling notifications formerly emitted as signals
   *
   * Module-internal explicit callbacks (wired by RenderManager / the facade).
   * cache_progress receives iterator fractions in [0,1];
   * stop_cache_proxy_tasks fires when a forced range finishes queueing.
   */
	void set_cache_progress_callback(std::function<void(double)> cb)
	{
		cache_progress_callback_ = std::move(cb);
	}

	void set_stop_cache_proxy_tasks_callback(std::function<void()> cb)
	{
		stop_cache_proxy_tasks_callback_ = std::move(cb);
	}

	/**
   * @brief Handler for when the RenderManager has returned rendered audio
   *
   * Formerly a private slot connected to RenderTicketWatcher::finished; now
   * registered as the watcher's explicit finished callback.
   */
	void audio_rendered(RenderTicketWatcher *watcher);

	/**
   * @brief Handler for when the RenderManager has returned rendered video frames
   */
	void video_rendered(RenderTicketWatcher *watcher);

	/**
   * @brief Handler for a completed audio conform
   *
   * Formerly connected to ConformManager::conform_ready; the facade (or the
   * codec wave) must call this when a conform finishes.
   */
	void conform_finished();

	/**
   * @brief Drop all state referencing the current (about-to-be-destroyed) project
   *
   * Replaces the Qt `Project::destroyed` connection. The facade owns Project
   * lifetime events and must call this before the Project is freed.
   */
	void project_destroyed();

	/**
   * @brief Generic function called whenever the frames to render need to be (re)queued
   */
	void try_render();

	/**
   * @brief Whether try_render() wants to be called again after requeue_delay()
   *
   * Replaces the single-shot delayed_requeue_timer_ (see class comment).
   */
	bool delayed_requeue_pending() const
	{
		return delayed_requeue_pending_;
	}

	void cancel_delayed_requeue()
	{
		delayed_requeue_pending_ = false;
	}

	int requeue_delay_ms() const
	{
		return requeue_delay_ms_;
	}

private:
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

	/**
   * @brief Handler for when a cache reports a video change over a time range
   *
   * Formerly a slot using sender(); the cache is now passed explicitly by the
   * PlaybackCache requested callback.
   */
	void video_invalidated_from_cache(PlaybackCache *cache,
								   ViewerOutput *context,
								   const olive::TimeRange &range);

	/**
   * @brief Handler for when a cache reports an audio change over a time range
   */
	void audio_invalidated_from_cache(PlaybackCache *cache,
								   ViewerOutput *context,
								   const olive::TimeRange &range);

	void cancel_for_cache(PlaybackCache *cache);

	void cache_proxy_task_cancelled();

	Project *project_;

	std::unique_ptr<ProjectCopier> copier_;

	TimeRange cache_range_;

	bool use_custom_range_;
	TimeRange custom_autocache_range_;

	bool pause_renders_;
	bool pause_thumbnails_;

	RenderTicketPtr single_frame_render_;
	std::map<RenderTicketWatcher *, std::vector<RenderTicketPtr>>
		video_immediate_passthroughs_;

	// Replaces the single-shot QTimer: when a copied node is not available yet,
	// try_render() sets this flag and the facade re-calls try_render() after
	// requeue_delay_.
	bool delayed_requeue_pending_;
	int requeue_delay_ms_;

	JobTime last_conform_task_;

	std::vector<RenderTicketWatcher *> running_video_tasks_;
	std::vector<RenderTicketWatcher *> running_audio_tasks_;

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

	std::map<PlaybackCache *, VideoCacheData> video_cache_data_;
	std::map<PlaybackCache *, AudioCacheData> audio_cache_data_;

	ColorProcessorPtr display_color_processor_;

	std::function<void(double)> cache_progress_callback_;
	std::function<void()> stop_cache_proxy_tasks_callback_;

	MultiCamNode *multicam_;

	bool ignore_cache_requests_;
};

}

#endif // OAK_AUTOCACHER_H
