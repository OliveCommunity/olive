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

#include "previewautocacher.h"

#include <algorithm>
#include <cstdio>

#include "audioplaybackcache.h"
#include "audiowaveformcache.h"
#include "diskmanager.h"
#include "framehashcache.h"
#include "inputdragger.h"
#include "input/multicam/multicamnode.h"
#include "playbackcache.h"
#include "project.h"
#include "qtutils.h"
#include "rendermanager.h"

namespace olive
{

PreviewAutoCacher::PreviewAutoCacher()
	: project_(nullptr)
	, use_custom_range_(false)
	, pause_renders_(false)
	, pause_thumbnails_(false)
	, single_frame_render_(nullptr)
	, delayed_requeue_pending_(false)
	, copied_color_manager_(nullptr)
	, multicam_(nullptr)
	, ignore_cache_requests_(false)
{
	copier_ = std::make_unique<ProjectCopier>();
	copier_->set_added_node_handler(
		[this](Node *n) { connect_to_node_cache(n); });
	copier_->set_removed_node_handler(
		[this](Node *n) { disconnect_from_node_cache(n); });

	// Set defaults
	set_playhead(0);

	// Wait a certain amount of time before requeuing when we receive an invalidate signal.
	// (Formerly a single-shot QTimer; now an explicit pending flag, see header.)
	requeue_delay_ms_ = OAK_CONFIG("AutoCacheDelay").toInt();

	// Conform notifications: the facade (codec wave) calls conform_finished()
	// when ConformManager reports a ready conform.
}

PreviewAutoCacher::~PreviewAutoCacher()
{
	// Ensure everything is cleaned up appropriately
	set_project(nullptr);
}

RenderTicketPtr PreviewAutoCacher::get_single_frame(ViewerOutput *viewer,
												  const Rational &t, bool dry)
{
	return get_single_frame(viewer->get_connected_texture_output(), viewer, t, dry);
}

RenderTicketPtr PreviewAutoCacher::get_single_frame(Node *n, ViewerOutput *viewer,
												  const Rational &t, bool dry)
{
	// If we have a single frame render queued (but not yet sent to the RenderManager), cancel it now
	cancel_queued_single_frame_render();

	// Create a new single frame render ticket
	auto sfr = std::make_shared<RenderTicket>();
	sfr->start();
	sfr->set_property("time", Variant::from_value(t));
	sfr->set_property("dry", dry);
	sfr->set_property("node", Variant::from_value(QtUtils::ptr_to_value(n)));
	sfr->set_property("viewer", Variant::from_value(QtUtils::ptr_to_value(viewer)));

	// Queue it and try to render
	single_frame_render_ = sfr;
	try_render();

	return sfr;
}

RenderTicketPtr PreviewAutoCacher::get_range_of_audio(ViewerOutput *viewer,
												   TimeRange range)
{
	Node *copy = copier_->get_copy(viewer->get_connected_sample_output());
	return render_audio(copy, viewer, range, nullptr);
}

void PreviewAutoCacher::clear_single_frame_renders()
{
	// Snapshot the watchers before doing anything that might synchronously
	// delete them (finishing a ticket runs video_rendered(), which deletes the
	// watcher and removes it from the map). A watcher pointer is only
	// dereferenced while it is still a key of the map, which is exactly the
	// liveness criterion (deletion always goes through map removal). This
	// replaces the former QPointer guarding.
	std::vector<RenderTicketWatcher *> watchers;
	for (auto it = video_immediate_passthroughs_.cbegin();
		 it != video_immediate_passthroughs_.cend(); it++) {
		watchers.push_back(it->first);
	}

	for (RenderTicketWatcher *w : watchers) {
		if (!video_immediate_passthroughs_.count(w)) {
			// Already finished (and deleted) by an earlier iteration
			continue;
		}

		// Keep already-running workers alive: cancelling an in-flight render
		// forces the worker process to be torn down, which defeats the process
		// pool. Frames that finish late are simply ignored by the viewer.
		if (w->is_running()) {
			continue;
		}

		RenderTicketPtr ticket = w->get_ticket();
		w->cancel();
		RenderManager::instance()->remove_ticket(ticket);
		ticket->finish();
	}
}

void PreviewAutoCacher::clear_single_frame_renders_that_arent_running()
{
	std::vector<RenderTicketWatcher *> watchers;
	for (auto it = video_immediate_passthroughs_.cbegin();
		 it != video_immediate_passthroughs_.cend(); it++) {
		watchers.push_back(it->first);
	}

	for (RenderTicketWatcher *w : watchers) {
		if (!video_immediate_passthroughs_.count(w) || w->is_running()) {
			continue;
		}

		RenderTicketPtr ticket = w->get_ticket();
		w->cancel();
		RenderManager::instance()->remove_ticket(ticket);
		ticket->finish();
	}
}

void PreviewAutoCacher::video_invalidated_from_cache(PlaybackCache *cache,
												  ViewerOutput *context,
												  const TimeRange &range)
{
	cache->clear_request_range(range);

	video_invalidated_from_node(context, cache, range);
}

void PreviewAutoCacher::audio_invalidated_from_cache(PlaybackCache *cache,
												  ViewerOutput *context,
												  const TimeRange &range)
{
	cache->clear_request_range(range);

	audio_invalidated_from_node(context, cache, range);
}

void PreviewAutoCacher::cancel_for_cache(PlaybackCache *cache)
{
	if (dynamic_cast<FrameHashCache *>(cache) ||
		dynamic_cast<ThumbnailCache *>(cache)) {
		for (auto it = pending_video_jobs_.begin();
			 it != pending_video_jobs_.end();) {
			if ((*it).cache == cache) {
				it = pending_video_jobs_.erase(it);
			} else {
				it++;
			}
		}
	} else if (dynamic_cast<AudioPlaybackCache *>(cache) ||
			   dynamic_cast<AudioWaveformCache *>(cache)) {
		for (auto it = pending_audio_jobs_.begin();
			 it != pending_audio_jobs_.end();) {
			if ((*it).cache == cache) {
				it = pending_audio_jobs_.erase(it);
			} else {
				it++;
			}
		}
	}
}

void PreviewAutoCacher::audio_rendered(RenderTicketWatcher *watcher)
{
	// If the task list doesn't contain this watcher, presumably it was cleared as a result of a
	// viewer switch, so we'll completely ignore this watcher
	auto task_it = std::find(running_audio_tasks_.begin(),
							 running_audio_tasks_.end(), watcher);
	if (task_it != running_audio_tasks_.end()) {
		running_audio_tasks_.erase(task_it);

		// Assume that a "result" is a fully completed image and a non-result is a cancelled ticket
		TimeRange range = watcher->property("time").value<TimeRange>();
		Node *node = copier_->get_original(QtUtils::value_to_ptr<Node>(
			uintptr_t(watcher->property("node").to_u_long_long())));

		if (watcher->has_result() && node) {
			if (PlaybackCache *cache = QtUtils::value_to_ptr<PlaybackCache>(
					uintptr_t(watcher->property("cache").to_u_long_long()))) {
				AudioCacheData &d = audio_cache_data_[cache];

				JobTime watcher_job_time =
					watcher->property("job").value<JobTime>();

				TimeRangeList valid_ranges =
					d.job_tracker.getCurrentSubRanges(range, watcher_job_time);

				AudioVisualWaveform waveform =
					watcher->get_ticket()
						->property("waveform")
						.value<AudioVisualWaveform>();

				SampleBuffer buf = watcher->get().value<SampleBuffer>();

				bool incomplete =
					watcher->get_ticket()->property("incomplete").to_bool();

				if (AudioPlaybackCache *pcm =
						dynamic_cast<AudioPlaybackCache *>(cache)) {
					// WritePCM is tolerant to its buffer being null, it will just write silence instead
					pcm->set_parameters(buf.audio_params());
					pcm->write_pcm(range, valid_ranges,
								  watcher->get().value<SampleBuffer>());
				} else if (AudioWaveformCache *wave =
							   dynamic_cast<AudioWaveformCache *>(cache)) {
					wave->set_parameters(buf.audio_params());
					if (!incomplete) {
						wave->write_waveform(range, valid_ranges, &waveform);
					}
				}

				if (incomplete) {
					if (last_conform_task_ > watcher_job_time) {
						// Requeue now
						cache->invalidate(range);
					} else {
						// Wait for conform
						d.needs_conform.insert(range);
					}
				}
			}
		}

		// Continue rendering
		try_render();
	}

	delete watcher;
}

void PreviewAutoCacher::video_rendered(RenderTicketWatcher *watcher)
{
	const StringList bad_cache_names =
		watcher->get_ticket()->property("badcache").to_string_list();
	if (!bad_cache_names.empty()) {
		for (const std::string &fn : bad_cache_names) {
			DiskManager::instance()->delete_specific_file(fn);
		}
	}

	// Process passthroughs no matter what, if the viewer was switched, the passthrough map would be
	// cleared anyway
	std::vector<RenderTicketPtr> tickets;
	{
		auto it = video_immediate_passthroughs_.find(watcher);
		if (it != video_immediate_passthroughs_.end()) {
			tickets = std::move(it->second);
			video_immediate_passthroughs_.erase(it);
		}
	}
	for (RenderTicketPtr &t : tickets) {
		if (watcher->has_result()) {
			t->set_property("multicam_output",
						   watcher->get_ticket()->property("multicam_output"));
			t->finish(watcher->get());
		} else {
			t->finish();
		}
	}

	// If the task list doesn't contain this watcher, presumably it was cleared as a result of a
	// viewer switch, so we'll completely ignore this watcher
	auto task_it = std::find(running_video_tasks_.begin(),
							 running_video_tasks_.end(), watcher);
	if (task_it != running_video_tasks_.end()) {
		running_video_tasks_.erase(task_it);

		// Assume that a "result" is a fully completed image and a non-result is a cancelled ticket
		if (watcher->has_result()) {
			if (watcher->get_ticket()->property("cached").to_bool()) {
				if (FrameHashCache *cache = QtUtils::value_to_ptr<FrameHashCache>(
						uintptr_t(watcher->property("cache").to_u_long_long()))) {
					Rational time = watcher->property("time").value<Rational>();
					JobTime job = watcher->property("job").value<JobTime>();

					auto data_it = video_cache_data_.find(cache);
					if (data_it != video_cache_data_.end() &&
						data_it->second.job_tracker.isCurrent(time, job)) {
						cache->validate_time(time);
					}
				}
			}
		}

		// Continue rendering
		try_render();
	}

	delete watcher;
}

void PreviewAutoCacher::connect_to_node_cache(Node *node)
{
	if (ignore_cache_requests_) {
		return;
	}

	PlaybackCache *video_cache = node->video_frame_cache();
	PlaybackCache *thumb_cache = node->thumbnail_cache();
	PlaybackCache *audio_cache = node->audio_playback_cache();
	PlaybackCache *wave_cache = node->waveform_cache();

	video_cache->set_requested_callback(
		[this, video_cache](ViewerOutput *context, const TimeRange &range) {
			video_invalidated_from_cache(video_cache, context, range);
		});

	thumb_cache->set_requested_callback(
		[this, thumb_cache](ViewerOutput *context, const TimeRange &range) {
			video_invalidated_from_cache(thumb_cache, context, range);
		});

	audio_cache->set_requested_callback(
		[this, audio_cache](ViewerOutput *context, const TimeRange &range) {
			audio_invalidated_from_cache(audio_cache, context, range);
		});

	wave_cache->set_requested_callback(
		[this, wave_cache](ViewerOutput *context, const TimeRange &range) {
			audio_invalidated_from_cache(wave_cache, context, range);
		});

	video_cache->set_cancel_all_callback(
		[this, video_cache]() { cancel_for_cache(video_cache); });

	audio_cache->set_cancel_all_callback(
		[this, audio_cache]() { cancel_for_cache(audio_cache); });

	node->video_frame_cache()->resignal_requests();
	node->thumbnail_cache()->resignal_requests();
	node->audio_playback_cache()->resignal_requests();
	node->waveform_cache()->resignal_requests();
}

void PreviewAutoCacher::disconnect_from_node_cache(Node *node)
{
	node->video_frame_cache()->set_requested_callback(nullptr);
	node->thumbnail_cache()->set_requested_callback(nullptr);
	node->audio_playback_cache()->set_requested_callback(nullptr);
	node->waveform_cache()->set_requested_callback(nullptr);

	node->video_frame_cache()->set_cancel_all_callback(nullptr);
	node->audio_playback_cache()->set_cancel_all_callback(nullptr);
}

void PreviewAutoCacher::cancel_queued_single_frame_render()
{
	if (single_frame_render_) {
		// Signal that this ticket was cancelled with no value
		single_frame_render_->finish();
		single_frame_render_ = nullptr;
	}
}

void PreviewAutoCacher::start_caching_range(const TimeRange &range,
										  TimeRangeList *range_list,
										  RenderJobTracker *tracker)
{
	range_list->insert(range);
	tracker->insert(range, copier_->get_graph_change_time());
}

void PreviewAutoCacher::start_caching_video_range(ViewerOutput *context,
											   PlaybackCache *cache,
											   const TimeRange &range)
{
	Node *node = cache->parent();
	Rational using_tb;
	if (ThumbnailCache *thumbs = dynamic_cast<ThumbnailCache *>(cache)) {
		using_tb = thumbs->get_timebase();
	} else {
		using_tb = context->get_video_params().frame_rate_as_time_base();
	}

	cache->clear_request_range(range);

	TimeRangeListFrameIterator iterator({ range }, using_tb);
	pending_video_jobs_.push_back({ node, context, cache, range, iterator });
	video_cache_data_[cache].job_tracker.insert(
		TimeRange(iterator.snap(range.in()), range.out()),
		copier_->get_graph_change_time());
	try_render();
}

void PreviewAutoCacher::start_caching_audio_range(ViewerOutput *context,
											   PlaybackCache *cache,
											   const TimeRange &range)
{
	Node *node = cache->parent();

	cache->clear_request_range(range);

	pending_audio_jobs_.push_back({ node, context, cache, range });
	AudioCacheData &data = audio_cache_data_[cache];
	data.context = context;
	data.job_tracker.insert(range, copier_->get_graph_change_time());
	try_render();
}

void PreviewAutoCacher::video_invalidated_from_node(ViewerOutput *context,
												 PlaybackCache *cache,
												 const TimeRange &range)
{
	// Ignore render requests if no video is present
	if (!context || !context->get_video_params().is_valid()) {
		return;
	}

	// Stop any current render tasks because a) they might be out of date now anyway, and b) we
	// want to dedicate all our rendering power to realtime feedback for the user
	//CancelVideoTasks(node);

	cache->clear_request_range(range);

	// If auto-cache is enabled and a slider is not being dragged, queue up to hash these frames
	if (!NodeInputDragger::is_input_being_dragged()) {
		start_caching_video_range(context, cache, range);
	}
}

void PreviewAutoCacher::audio_invalidated_from_node(ViewerOutput *context,
												 PlaybackCache *cache,
												 const TimeRange &range)
{
	// Ignore render requests if no video is present
	if (!context || !context->get_audio_params().is_valid()) {
		return;
	}

	// We don't stop rendering audio because currently there's no system of requeuing audio if it's
	// cancelled, so some areas may end up unrendered forever
	//  ClearAudioQueue();

	cache->clear_request_range(range);

	// If we're auto-caching audio or require realtime waveforms, we'll have to render this
	start_caching_audio_range(context, cache, range);
}

void PreviewAutoCacher::set_playhead(const Rational &playhead)
{
	cache_range_ =
		TimeRange(playhead - OAK_CONFIG("DiskCacheBehind").value<Rational>(),
				  playhead + OAK_CONFIG("DiskCacheAhead").value<Rational>());

	try_render();
}

template <typename T> void cancel_tasks(const T &task_list, bool and_wait)
{
	for (auto it = task_list.cbegin(); it != task_list.cend(); it++) {
		// Signal that the ticket should not be finished
		(*it)->cancel();
	}

	if (and_wait) {
		// Wait for each ticket to finish
		for (auto it = task_list.cbegin(); it != task_list.cend(); it++) {
			(*it)->wait_for_finished();
		}
	}
}

void PreviewAutoCacher::cancel_video_tasks(bool and_wait_for_them_to_finish)
{
	cancel_tasks(running_video_tasks_, and_wait_for_them_to_finish);
}

void PreviewAutoCacher::cancel_audio_tasks(bool and_wait_for_them_to_finish)
{
	cancel_tasks(running_audio_tasks_, and_wait_for_them_to_finish);
}

bool PreviewAutoCacher::is_rendering_custom_range() const
{
	if (!use_custom_range_) {
		return false;
	}

	for (const VideoJob &job : pending_video_jobs_) {
		if (job.range == custom_autocache_range_ && job.iterator.has_next()) {
			return true;
		}
	}

	return false;
}

void PreviewAutoCacher::set_renders_paused(bool e)
{
	pause_renders_ = e;
	if (!e) {
		try_render();
	}
}

void PreviewAutoCacher::set_thumbnails_paused(bool e)
{
	pause_thumbnails_ = e;
	if (!e) {
		try_render();
	}
}

void PreviewAutoCacher::try_render()
{
	delayed_requeue_pending_ = false;

	if (copier_->has_updates_in_queue()) {
		// Check if we have jobs running in other threads that shouldn't be interrupted right now
		// NOTE: We don't check for downloads because, while they run in another thread, they don't
		//       require any access to the graph and therefore don't risk race conditions.
		if (!running_audio_tasks_.empty() ||
			!running_video_tasks_.empty()) {
			return;
		}

		// No jobs are active, we can process the update queue
		copier_->process_update_queue();
	}

	if (single_frame_render_) {
		// Make an explicit copy of the render ticket here - it seems that on some systems it can be set
		// to NULL before we're done with it...
		RenderTicketPtr t = single_frame_render_;
		single_frame_render_ = nullptr;

		// Check if already caching this
		Node *n = QtUtils::value_to_ptr<Node>(
			uintptr_t(t->property("node").to_u_long_long()));
		Node *copy = copier_->get_copy(n);

		if (copy) {
			RenderTicketWatcher *watcher = render_frame(
				copy,
				QtUtils::value_to_ptr<ViewerOutput>(
					uintptr_t(t->property("viewer").to_u_long_long())),
				t->property("time").value<Rational>(), nullptr,
				t->property("dry").to_bool());
			if (watcher) {
				video_immediate_passthroughs_[watcher].push_back(t);
			}
		} else {
			fprintf(stderr,
					"Failed to find copied node for SFR ticket, requeueing\n");
			single_frame_render_ = t;
			delayed_requeue_pending_ = true;
		}
	}

	if (!pause_renders_) {
		// Completely arbitrary number. I don't know what's optimal for this yet.
		const int max_tasks = 4;

		// Handle video tasks
		if (!pause_thumbnails_) {
			while (!pending_video_jobs_.empty()) {
				VideoJob &d = pending_video_jobs_.front();

				if (Node *copy = copier_->get_copy(d.node)) {
					// Queue next frames
					Rational t;
					while (running_video_tasks_.size() < size_t(max_tasks) &&
						   d.iterator.get_next(&t)) {
						render_frame(copy, d.context, t, d.cache, false);

						if (cache_progress_callback_) {
							cache_progress_callback_(
								double(d.iterator.frame_index()) /
								double(d.iterator.size()));
						}

						if (!d.iterator.has_next() &&
							stop_cache_proxy_tasks_callback_) {
							stop_cache_proxy_tasks_callback_();
						}
					}
				} else {
					fprintf(stderr,
							"Failed to find node copy for video job, retrying\n");
					delayed_requeue_pending_ = true;
					break;
				}

				if (d.iterator.has_next()) {
					break;
				} else {
					pending_video_jobs_.pop_front();
				}
			}
		}

		// Handle audio tasks
		while (!pending_audio_jobs_.empty() &&
			   running_audio_tasks_.size() < size_t(max_tasks)) {
			AudioJob &d = pending_audio_jobs_.front();

			bool pop = true;

			// Start job
			if (Node *copy = copier_->get_copy(d.node)) {
				TimeRange &queued_range = d.range;
				TimeRange use_range = queued_range;

				if (dynamic_cast<AudioWaveformCache *>(d.cache)) {
					Rational new_out = std::min(
						use_range.in() +
							AudioVisualWaveform::k_minimum_sample_rate.flipped(),
						use_range.out());

					if (new_out != use_range.out()) {
						use_range.set_out(new_out);
						queued_range.set_in(new_out);
						pop = false;
					}
				}

				render_audio(copy, d.context, use_range, d.cache);
			} else {
				fprintf(stderr,
						"Failed to find node copy for audio job, retrying\n");
				pop = false;
				delayed_requeue_pending_ = true;
				break;
			}

			if (pop) {
				pending_audio_jobs_.pop_front();
			}
		}
	}
}

RenderTicketWatcher *PreviewAutoCacher::render_frame(Node *node,
													ViewerOutput *context,
													const Rational &time,
													PlaybackCache *cache,
													bool dry)
{
	RenderTicketWatcher *watcher = new RenderTicketWatcher();
	watcher->set_property("job",
						 Variant::from_value(copier_->get_last_update_time()));
	watcher->set_property("cache",
						 Variant::from_value(QtUtils::ptr_to_value(cache)));
	watcher->set_property("time", Variant::from_value(time));
	watcher->set_finished_callback(
		[this](RenderTicketWatcher *w) { video_rendered(w); });

	running_video_tasks_.push_back(watcher);

	RenderManager::RenderVideoParams rvp(node, context->get_video_params(),
										 context->get_audio_params(), time,
										 copied_color_manager_,
										 RenderMode::k_offline);

	if (FrameHashCache *frame_cache = dynamic_cast<FrameHashCache *>(cache)) {
		if (ThumbnailCache *wave_cache =
				dynamic_cast<ThumbnailCache *>(cache)) {
			(void) wave_cache;
			rvp.video_params.set_divider(
				VideoParams::get_divider_for_target_resolution(
					rvp.video_params.width(), rvp.video_params.height(), 160,
					120));
			rvp.force_format = PixelFormat::f32;
			rvp.force_channel_count = VideoParams::k_rgba_channel_count;
		} else {
			frame_cache->set_timebase(
				context->get_video_params().frame_rate_as_time_base());
		}

		rvp.add_cache(frame_cache);
	} else {
		// Preview/display frames are rendered at reduced precision to cut the
		// GPU->CPU readback and IPC transfer bandwidth. The internal render
		// pipeline stays F32/ACEScg; the final preview copy is packed 10-bit
		// RGBA (4 bytes/pixel) to preserve 10-bit panel precision while halving
		// bandwidth compared to F16.
		rvp.force_format = PixelFormat::u10;
		rvp.force_channel_count = VideoParams::k_rgba_channel_count;
	}

	// Video playback frames are rendered out-of-process. GPU textures cannot be
	// shared across worker processes (or across independent Vulkan instances),
	// so we always request CPU frames.
	rvp.return_type = dry ? RenderManager::k_null : RenderManager::k_frame;

	// Allow using cached images for this render job
	rvp.use_cache = true;

	// Multicam
	rvp.multicam = copier_->get_copy(multicam_);

	watcher->set_ticket(RenderManager::instance()->render_frame(rvp));

	// If the ticket finished synchronously, video_rendered has already deleted the
	// watcher. The caller must not use this pointer in that case.
	if (std::find(running_video_tasks_.begin(), running_video_tasks_.end(),
				  watcher) == running_video_tasks_.end()) {
		return nullptr;
	}

	return watcher;
}

RenderTicketPtr PreviewAutoCacher::render_audio(Node *node,
											   ViewerOutput *context,
											   const TimeRange &r,
											   PlaybackCache *cache)
{
	RenderTicketWatcher *watcher = new RenderTicketWatcher();
	watcher->set_property("job",
						 Variant::from_value(copier_->get_last_update_time()));
	watcher->set_property("node",
						 Variant::from_value(QtUtils::ptr_to_value(node)));
	watcher->set_property("cache",
						 Variant::from_value(QtUtils::ptr_to_value(cache)));
	watcher->set_property("time", Variant::from_value(r));
	watcher->set_finished_callback(
		[this](RenderTicketWatcher *w) { audio_rendered(w); });
	running_audio_tasks_.push_back(watcher);

	AudioParams p = context->get_audio_params();
	const bool invalid_params =
		(p.sample_rate() <= 0 || p.channel_count() <= 0);
	if (invalid_params) {
		AudioParams fallback(
			OAK_CONFIG("DefaultSequenceAudioFrequency").toInt(),
			OAK_CONFIG("DefaultSequenceAudioLayout").toULongLong(),
			ViewerOutput::k_default_sample_format);
		p = fallback;
	}
	p.set_format(ViewerOutput::k_default_sample_format);

	RenderManager::RenderAudioParams rap(node, r, p, RenderMode::k_offline);

	rap.generate_waveforms = dynamic_cast<AudioWaveformCache *>(cache);
	rap.clamp = false;

	RenderTicketPtr ticket = RenderManager::instance()->render_audio(rap);
	watcher->set_ticket(ticket);
	return ticket;
}

void PreviewAutoCacher::conform_finished()
{
	// Got an audio conform, requeue all the audio currently needing a conform
	last_conform_task_.acquire();

	for (auto it = audio_cache_data_.begin(); it != audio_cache_data_.end();
		 it++) {
		if (!it->first || !it->second.context) {
			continue;
		}

		for (const TimeRange &range : it->second.needs_conform) {
			it->first->request(it->second.context, range);
		}
		it->second.needs_conform.clear();
	}
}

void PreviewAutoCacher::cache_proxy_task_cancelled()
{
	pending_video_jobs_.clear();

	try_render();
}

void PreviewAutoCacher::force_cache_range(ViewerOutput *context,
										const TimeRange &range)
{
	use_custom_range_ = true;
	custom_autocache_range_ = range;

	// Re-hash these frames and start rendering
	start_caching_video_range(context, context->video_frame_cache(), range);
}

void PreviewAutoCacher::project_destroyed()
{
	// If the project dies while we're still using it (e.g. shutdown order:
	// project freed before the RenderManager), drop all state that
	// references its nodes/caches without touching them. Otherwise the
	// next set_project(nullptr) would clear callbacks on dead caches.
	project_ = nullptr;
	delayed_requeue_pending_ = false;
	single_frame_render_ = nullptr;
	video_immediate_passthroughs_.clear();
	pending_video_jobs_.clear();
	pending_audio_jobs_.clear();
	video_cache_data_.clear();
	audio_cache_data_.clear();
	multicam_ = nullptr;
	// The copier's own destroyed-guard nulls its original_; this just
	// clears its copy maps without touching the dead project.
	copier_->set_project(nullptr);
}

void PreviewAutoCacher::set_project(Project *project)
{
	if (project_ == project) {
		return;
	}

	if (project_) {
		// We must wait for any jobs to finish because they'll be using our copied graph and we're
		// about to destroy it

		// Stop requeue if it's pending
		delayed_requeue_pending_ = false;

		// Handle video rendering tasks
		if (!running_video_tasks_.empty()) {
			// Cancel any video tasks and wait for them to finish
			cancel_video_tasks(true);
			running_video_tasks_.clear();
		}

		// Handle audio rendering tasks
		if (!running_audio_tasks_.empty()) {
			// Cancel any audio tasks and wait for them to finish
			cancel_audio_tasks(true);
			running_audio_tasks_.clear();
		}

		// Clear any single frame render that might be queued
		cancel_queued_single_frame_render();

		// Not interested in video passthroughs anymore
		video_immediate_passthroughs_.clear();

		// Disconnect from all node cache's
		for (auto it = copier_->get_node_map().cbegin();
			 it != copier_->get_node_map().cend(); it++) {
			disconnect_from_node_cache(it->first);
		}

		// Delete all of our copied nodes
		copier_->set_project(nullptr);

		// Ensure all cache data is cleared
		video_cache_data_.clear();
		audio_cache_data_.clear();

		// Clear multicam reference
		multicam_ = nullptr;
	}

	project_ = project;

	if (project_) {
		// NOTE: the facade must call project_destroyed() if the Project is
		// destroyed while set (replaces the former Project::destroyed
		// connection).

		// Copy graph (this should always be a Project)
		set_renders_paused(true);

		copier_->set_project(project_);

		for (size_t i = 0; i < project_->nodes().size(); i++) {
			project_->nodes().at(i)->ConnectedToPreviewEvent();
		}

		// Find copied viewer node
		copied_color_manager_ = copier_->get_copied_project()->color_manager();

		set_renders_paused(false);
	}
}

}
