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

#include <QPointer>
#include <QtConcurrent/QtConcurrent>

#include "codec/conformmanager.h"
#include "node/input/multicam/multicamnode.h"
#include "node/inputdragger.h"
#include "node/project.h"
#include "render/diskmanager.h"
#include "render/rendermanager.h"

namespace olive
{

PreviewAutoCacher::PreviewAutoCacher(QObject *parent)
	: QObject(parent)
	, project_(nullptr)
	, use_custom_range_(false)
	, pause_renders_(false)
	, pause_thumbnails_(false)
	, single_frame_render_(nullptr)
	, display_color_processor_(nullptr)
	, multicam_(nullptr)
	, ignore_cache_requests_(false)
{
	copier_ = new ProjectCopier(this);
	connect(copier_, &ProjectCopier::added_node, this,
			&PreviewAutoCacher::connect_to_node_cache);
	connect(copier_, &ProjectCopier::removed_node, this,
			&PreviewAutoCacher::disconnect_from_node_cache);

	// Set defaults
	set_playhead(0);

	// Wait a certain amount of time before requeuing when we receive an invalidate signal
	delayed_requeue_timer_.setInterval(OAK_CONFIG("AutoCacheDelay").toInt());
	delayed_requeue_timer_.setSingleShot(true);
	connect(&delayed_requeue_timer_, &QTimer::timeout, this,
			&PreviewAutoCacher::try_render);

	// Catch when a conform is ready
	connect(ConformManager::instance(), &ConformManager::conform_ready, this,
			&PreviewAutoCacher::conform_finished);
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
	sfr->setProperty("time", QVariant::fromValue(t));
	sfr->setProperty("dry", dry);
	sfr->setProperty("node", QtUtils::ptr_to_value(n));
	sfr->setProperty("viewer", QtUtils::ptr_to_value(viewer));

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
	// Snapshot the watchers as guarded pointers before doing anything that
	// might synchronously delete them (emitting Finished runs VideoRendered,
	// which deletes the watcher and removes it from the map). Iterating over a
	// raw-pointer copy of the map would leave dangling pointers.
	QList<QPointer<RenderTicketWatcher>> watchers;
	for (auto it = video_immediate_passthroughs_.cbegin();
		 it != video_immediate_passthroughs_.cend(); it++) {
		watchers.append(it.key());
	}

	foreach (const QPointer<RenderTicketWatcher> &w, watchers) {
		if (!w) {
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
		emit ticket->finished();
	}
}

void PreviewAutoCacher::clear_single_frame_renders_that_arent_running()
{
	QList<QPointer<RenderTicketWatcher>> watchers;
	for (auto it = video_immediate_passthroughs_.cbegin();
		 it != video_immediate_passthroughs_.cend(); it++) {
		watchers.append(it.key());
	}

	foreach (const QPointer<RenderTicketWatcher> &w, watchers) {
		if (!w || w->is_running()) {
			continue;
		}

		RenderTicketPtr ticket = w->get_ticket();
		w->cancel();
		RenderManager::instance()->remove_ticket(ticket);
		emit ticket->finished();
	}
}

void PreviewAutoCacher::video_invalidated_from_cache(ViewerOutput *context,
												  const TimeRange &range)
{
	PlaybackCache *cache = static_cast<PlaybackCache *>(sender());

	cache->clear_request_range(range);

	video_invalidated_from_node(context, cache, range);
}

void PreviewAutoCacher::audio_invalidated_from_cache(ViewerOutput *context,
												  const TimeRange &range)
{
	PlaybackCache *cache = static_cast<PlaybackCache *>(sender());

	cache->clear_request_range(range);

	audio_invalidated_from_node(context, cache, range);
}

void PreviewAutoCacher::cancel_for_cache()
{
	PlaybackCache *cache = static_cast<PlaybackCache *>(sender());

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

void PreviewAutoCacher::audio_rendered()
{
	// Receive watcher
	RenderTicketWatcher *watcher = static_cast<RenderTicketWatcher *>(sender());

	// If the task list doesn't contain this watcher, presumably it was cleared as a result of a
	// viewer switch, so we'll completely ignore this watcher
	if (running_audio_tasks_.removeOne(watcher)) {
		// Assume that a "result" is a fully completed image and a non-result is a cancelled ticket
		TimeRange range = watcher->property("time").value<TimeRange>();
		Node *node = copier_->get_original(
			QtUtils::value_to_ptr<Node>(watcher->property("node")));

		if (watcher->has_result() && node) {
			if (PlaybackCache *cache = QtUtils::value_to_ptr<PlaybackCache>(
					watcher->property("cache"))) {
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
					watcher->get_ticket()->property("incomplete").toBool();

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

void PreviewAutoCacher::video_rendered()
{
	RenderTicketWatcher *watcher = static_cast<RenderTicketWatcher *>(sender());

	const QStringList bad_cache_names =
		watcher->get_ticket()->property("badcache").toStringList();
	if (!bad_cache_names.empty()) {
		for (const QString &fn : bad_cache_names) {
			DiskManager::instance()->delete_specific_file(fn);
		}
	}

	// Process passthroughs no matter what, if the viewer was switched, the passthrough map would be
	// cleared anyway
	QVector<RenderTicketPtr> tickets =
		video_immediate_passthroughs_.take(watcher);
	foreach (RenderTicketPtr t, tickets) {
		if (watcher->has_result()) {
			t->setProperty("multicam_output",
						   watcher->get_ticket()->property("multicam_output"));
			t->finish(watcher->get());
		} else {
			t->finish();
		}
	}

	// If the task list doesn't contain this watcher, presumably it was cleared as a result of a
	// viewer switch, so we'll completely ignore this watcher
	if (running_video_tasks_.removeOne(watcher)) {
		// Assume that a "result" is a fully completed image and a non-result is a cancelled ticket
		if (watcher->has_result()) {
			if (watcher->get_ticket()->property("cached").toBool()) {
				if (FrameHashCache *cache = QtUtils::value_to_ptr<FrameHashCache>(
						watcher->property("cache"))) {
					Rational time = watcher->property("time").value<Rational>();
					JobTime job = watcher->property("job").value<JobTime>();

					if (video_cache_data_.value(cache).job_tracker.isCurrent(
							time, job)) {
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

	connect(node->video_frame_cache(), &PlaybackCache::requested, this,
			&PreviewAutoCacher::video_invalidated_from_cache);

	connect(node->thumbnail_cache(), &PlaybackCache::requested, this,
			&PreviewAutoCacher::video_invalidated_from_cache);

	connect(node->audio_playback_cache(), &PlaybackCache::requested, this,
			&PreviewAutoCacher::audio_invalidated_from_cache);

	connect(node->waveform_cache(), &PlaybackCache::requested, this,
			&PreviewAutoCacher::audio_invalidated_from_cache);

	connect(node->video_frame_cache(), &PlaybackCache::cancel_all, this,
			&PreviewAutoCacher::cancel_for_cache);

	connect(node->audio_playback_cache(), &PlaybackCache::cancel_all, this,
			&PreviewAutoCacher::cancel_for_cache);

	node->video_frame_cache()->resignal_requests();
	node->thumbnail_cache()->resignal_requests();
	node->audio_playback_cache()->resignal_requests();
	node->waveform_cache()->resignal_requests();
}

void PreviewAutoCacher::disconnect_from_node_cache(Node *node)
{
	disconnect(node->video_frame_cache(), &PlaybackCache::requested, this,
			   &PreviewAutoCacher::video_invalidated_from_cache);

	disconnect(node->thumbnail_cache(), &PlaybackCache::requested, this,
			   &PreviewAutoCacher::video_invalidated_from_cache);

	disconnect(node->audio_playback_cache(), &PlaybackCache::requested, this,
			   &PreviewAutoCacher::audio_invalidated_from_cache);

	disconnect(node->waveform_cache(), &PlaybackCache::requested, this,
			   &PreviewAutoCacher::audio_invalidated_from_cache);

	disconnect(node->video_frame_cache(), &PlaybackCache::cancel_all, this,
			   &PreviewAutoCacher::cancel_for_cache);

	disconnect(node->audio_playback_cache(), &PlaybackCache::cancel_all, this,
			   &PreviewAutoCacher::cancel_for_cache);
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
	delayed_requeue_timer_.stop();

	if (copier_->has_updates_in_queue()) {
		// Check if we have jobs running in other threads that shouldn't be interrupted right now
		// NOTE: We don't check for downloads because, while they run in another thread, they don't
		//       require any access to the graph and therefore don't risk race conditions.
		if (!running_audio_tasks_.isEmpty() ||
			!running_video_tasks_.isEmpty()) {
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
		Node *n = QtUtils::value_to_ptr<Node>(t->property("node"));
		Node *copy = copier_->get_copy(n);

		if (copy) {
			RenderTicketWatcher *watcher = render_frame(
				copy, QtUtils::value_to_ptr<ViewerOutput>(t->property("viewer")),
				t->property("time").value<Rational>(), nullptr,
				t->property("dry").toBool());
			if (watcher) {
				video_immediate_passthroughs_[watcher].append(t);
			}
		} else {
			qWarning()
				<< "Failed to find copied node for SFR ticket, requeueing";
			single_frame_render_ = t;
			if (!delayed_requeue_timer_.isActive()) {
				delayed_requeue_timer_.start();
			}
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
					while (running_video_tasks_.size() < max_tasks &&
						   d.iterator.get_next(&t)) {
						render_frame(copy, d.context, t, d.cache, false);

						emit signal_cache_proxy_task_progress(
							double(d.iterator.frame_index()) /
							double(d.iterator.size()));

						if (!d.iterator.has_next()) {
							emit stop_cache_proxy_tasks();
						}
					}
				} else {
					qWarning()
						<< "Failed to find node copy for video job, retrying";
					if (!delayed_requeue_timer_.isActive()) {
						delayed_requeue_timer_.start();
					}
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
			   running_audio_tasks_.size() < max_tasks) {
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
				qWarning()
					<< "Failed to find node copy for audio job, retrying";
				pop = false;
				if (!delayed_requeue_timer_.isActive()) {
					delayed_requeue_timer_.start();
				}
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
	watcher->setProperty("job",
						 QVariant::fromValue(copier_->get_last_update_time()));
	watcher->setProperty("cache", QtUtils::ptr_to_value(cache));
	watcher->setProperty("time", QVariant::fromValue(time));
	connect(watcher, &RenderTicketWatcher::finished, this,
			&PreviewAutoCacher::video_rendered);

	running_video_tasks_.append(watcher);

	RenderManager::RenderVideoParams rvp(node, context->get_video_params(),
										 context->get_audio_params(), time,
										 copied_color_manager_,
										 RenderMode::k_offline);

	if (FrameHashCache *frame_cache = dynamic_cast<FrameHashCache *>(cache)) {
		if (ThumbnailCache *wave_cache =
				dynamic_cast<ThumbnailCache *>(cache)) {
			Q_UNUSED(wave_cache)
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

	// If the ticket finished synchronously, VideoRendered has already deleted the
	// watcher. The caller must not use this pointer in that case.
	if (!running_video_tasks_.contains(watcher)) {
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
	watcher->setProperty("job",
						 QVariant::fromValue(copier_->get_last_update_time()));
	watcher->setProperty("node", QtUtils::ptr_to_value(node));
	watcher->setProperty("cache", QtUtils::ptr_to_value(cache));
	watcher->setProperty("time", QVariant::fromValue(r));
	connect(watcher, &RenderTicketWatcher::finished, this,
			&PreviewAutoCacher::audio_rendered);
	running_audio_tasks_.append(watcher);

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
		if (!it.key() || !it.value().context) {
			continue;
		}

		for (const TimeRange &range : it.value().needs_conform) {
			it.key()->request(it.value().context, range);
		}
		it.value().needs_conform.clear();
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

void PreviewAutoCacher::set_project(Project *project)
{
	if (project_ == project) {
		return;
	}

	if (project_) {
		// We must wait for any jobs to finish because they'll be using our copied graph and we're
		// about to destroy it

		// Stop requeue timer if it's running
		delayed_requeue_timer_.stop();

		// Handle video rendering tasks
		if (!running_video_tasks_.isEmpty()) {
			// Cancel any video tasks and wait for them to finish
			cancel_video_tasks(true);
			running_video_tasks_.clear();
		}

		// Handle audio rendering tasks
		if (!running_audio_tasks_.isEmpty()) {
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
			disconnect_from_node_cache(it.key());
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
		// If the project dies while we're still using it (e.g. shutdown order:
		// project freed before the RenderManager), drop all state that
		// references its nodes/caches without touching them. Otherwise the
		// next set_project(nullptr) would disconnect signals on dead caches.
		connect(project_, &Project::destroyed, this, [this]() {
			project_ = nullptr;
			delayed_requeue_timer_.stop();
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
		});

		// Copy graph (this should always be a Project)
		set_renders_paused(true);

		copier_->set_project(project_);

		for (int i = 0; i < project_->nodes().size(); i++) {
			project_->nodes().at(i)->ConnectedToPreviewEvent();
		}

		// Find copied viewer node
		copied_color_manager_ = copier_->get_copied_project()->color_manager();

		set_renders_paused(false);
	}
}

}
