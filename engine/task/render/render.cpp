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

#include "render.h"

#include "node/project/sequence/sequence.h"
#include "render/rendermanager.h"

namespace olive
{

RenderTask::RenderTask()
	: running_tickets_(0)
	, native_progress_signalling_(true)
{
}

RenderTask::~RenderTask()
{
}

bool RenderTask::render(ColorManager *manager, const TimeRangeList &video_range,
						const TimeRangeList &audio_range,
						const TimeRange &subtitle_range, RenderMode::Mode mode,
						FrameHashCache *cache, const QSize &force_size,
						const QMatrix4x4 &force_matrix,
						PixelFormat force_format, int force_channel_count,
						ColorProcessorPtr force_color_output,
						const ColorTransform &force_color_transform)
{
	QMetaObject::invokeMethod(RenderManager::instance(),
							  "SetAggressiveGarbageCollection",
							  Q_ARG(bool, true));

	// Run watchers in another thread so they can accept signals even while this thread is blocked
	QThread watcher_thread;
	watcher_thread.start();

	double progress_counter = 0;
	double total_length = 0;

	// Store real time before any rendering takes place
	// Queue audio jobs
	for (const TimeRange &range : audio_range) {
		// Don't count audio progress, since it's generally a lot faster than video and is weighted at
		// 50%, which makes the progress bar look weird to the uninitiated
		//total_length += r.length().toDouble();

		RenderManager::RenderAudioParams rap(
			viewer_->get_connected_sample_output(), range, audio_params_,
			RenderMode::k_online);

		RenderTicketWatcher *watcher = new RenderTicketWatcher();
		watcher->setProperty("range", QVariant::fromValue(range));
		prepare_watcher(watcher, &watcher_thread);
		increment_running_tickets();
		watcher->set_ticket(RenderManager::instance()->render_audio(rap));
	}

	// Look up hashes
	TimeRangeListFrameIterator iterator(
		video_range, video_params().frame_rate_as_time_base());
	total_number_of_frames_ = iterator.size();
	total_length += total_number_of_frames_;

	// Start a render of a limited amount, and then render one frame for each frame that gets
	// finished. This prevents rendered frames from stacking up in memory indefinitely while the
	// encoder is processing them. The amount is kind of arbitrary, but we use the thread count so
	// each of the system's threads are utilized as memory allows.
	const int maximum_rendered_frames = QThread::idealThreadCount();

	Rational next_frame;
	for (int i = 0;
		 i < maximum_rendered_frames && iterator.get_next(&next_frame); i++) {
		start_ticket(&watcher_thread, manager, next_frame, mode, cache,
					force_size, force_matrix, force_format, force_channel_count,
					force_color_output, force_color_transform);
	}

	bool result = true;

	// Subtitle loop, loops over all blocks in sequence on all tracks
	if (!subtitle_range.length().isNull()) {
		if (Sequence *sequence = dynamic_cast<Sequence *>(viewer_)) {
			TrackList *list = sequence->track_list(Track::k_subtitle);
			QVector<int> block_indexes(list->get_track_count(), 0);

			QVector<int> tracks_to_push;
			do {
				tracks_to_push.clear();

				for (int i = 0; i < block_indexes.size(); i++) {
					Track *this_track = list->get_track_at(i);
					if (this_track->is_muted()) {
						continue;
					}

					int &this_block_index = block_indexes[i];
					if (this_block_index >= this_track->blocks().size()) {
						continue;
					}
					Block *this_block =
						this_track->blocks().at(this_block_index);

					Track *compare_track =
						tracks_to_push.isEmpty() ?
							nullptr :
							list->get_track_at(tracks_to_push.first());
					const int &compare_block_index =
						tracks_to_push.isEmpty() ?
							-1 :
							block_indexes.at(tracks_to_push.first());
					Block *compare_block =
						compare_track ?
							compare_track->blocks().at(compare_block_index) :
							nullptr;
					if (!compare_track ||
						compare_block->in() >= this_block->in()) {
						if (compare_track &&
							compare_block->in() != this_block->in()) {
							tracks_to_push.clear();
						}
						tracks_to_push.append(i);
					}
				}

				for (int i = 0; i < tracks_to_push.size(); i++) {
					Track *this_track = list->get_track_at(tracks_to_push.at(i));
					Block *this_block = this_track->blocks().at(
						block_indexes.at(tracks_to_push.at(i)));

					if (const SubtitleBlock *sub =
							dynamic_cast<const SubtitleBlock *>(this_block)) {
						if (sub->is_enabled()) {
							if (!encode_subtitle(sub)) {
								result = false;
								break;
							}
						}
					}

					block_indexes[tracks_to_push.at(i)]++;
				}
			} while (!tracks_to_push.isEmpty());
		}
	}

	finished_watcher_mutex_.lock();

	while (result && !is_cancelled()) {
		while (!finished_watchers_.empty() && !is_cancelled() && result) {
			RenderTicketWatcher *watcher = finished_watchers_.front();
			finished_watchers_.pop_front();

			finished_watcher_mutex_.unlock();

			// Analyze watcher here
			RenderManager::TicketType ticket_type =
				watcher->get_ticket()
					->property("type")
					.value<RenderManager::TicketType>();

			if (ticket_type == RenderManager::k_type_audio) {
				TimeRange range = watcher->property("range").value<TimeRange>();

				if (!audio_downloaded(range,
									 watcher->get().value<SampleBuffer>())) {
					result = false;
				}

				// Don't count audio progress, since it's generally a lot faster than video and is weighted at
				// 50%, which makes the progress bar look weird to the uninitiated
				//progress_counter += range.length().toDouble();
				//emit ProgressChanged(progress_counter / total_length);

			} else if (ticket_type == RenderManager::k_type_video &&
					   two_step_frame_rendering()) {
				if (!download_frame(
						&watcher_thread, watcher->get().value<FramePtr>(),
						watcher->property("time").value<Rational>())) {
					result = false;
				}

				if (native_progress_signalling_) {
					progress_counter += 0.5;
					emit progress_changed(progress_counter / total_length);
				}

			} else {
				// Assume single-step video or video download ticket
				if (!frame_downloaded(
						watcher->get().value<FramePtr>(),
						watcher->property("time").value<Rational>())) {
					result = false;
				}

				if (native_progress_signalling_) {
					double progress_to_add = 1.0;
					if (two_step_frame_rendering()) {
						progress_to_add *= 0.5;
					}
					progress_counter += progress_to_add;

					emit progress_changed(progress_counter / total_length);
				}

				if (iterator.get_next(&next_frame)) {
					start_ticket(&watcher_thread, manager, next_frame, mode,
								cache, force_size, force_matrix, force_format,
								force_channel_count, force_color_output,
								force_color_transform);
				}
			}

			delete watcher;
			running_watchers_.removeOne(watcher);

			finished_watcher_mutex_.lock();
		}

		if (is_cancelled() || !result) {
			break;
		}

		// Run out of finished watchers. If we still have running tickets, wait for the next one to finish.
		if (running_tickets_ > 0) {
			finished_watcher_wait_cond_.wait(&finished_watcher_mutex_);
		} else {
			// No more running tickets or finished tickets, wem ust be
			break;
		}
	}

	finished_watcher_mutex_.unlock();

	if (is_cancelled() || !result) {
		// Cancel every watcher we created
		foreach (RenderTicketWatcher *watcher, running_watchers_) {
			watcher->cancel();
			disconnect(watcher, &RenderTicketWatcher::finished, this,
					   &RenderTask::ticket_done);
			RenderManager::instance()->remove_ticket(watcher->get_ticket());
		}

		foreach (RenderTicketWatcher *watcher, running_watchers_) {
			watcher->wait_for_finished();
		}
	}

	watcher_thread.quit();
	watcher_thread.wait();

	QMetaObject::invokeMethod(RenderManager::instance(),
							  "SetAggressiveGarbageCollection",
							  Q_ARG(bool, false));

	return result;
}

bool RenderTask::download_frame(QThread *thread, FramePtr frame,
							   const Rational &time)
{
	//RenderTicketWatcher* watcher = new RenderTicketWatcher();
	//PrepareWatcher(watcher, thread);

	//IncrementRunningTickets();

	//watcher->SetTicket(RenderManager::instance()->SaveFrameToCache(viewer_->video_frame_cache(), frame, time));

	// NOTE: Doesn't reflect the actual return result of SaveFrameToCache
	return true;
}

bool RenderTask::encode_subtitle(const SubtitleBlock *subtitle)
{
	Q_UNUSED(subtitle)
	return true;
}

void RenderTask::prepare_watcher(RenderTicketWatcher *watcher, QThread *thread)
{
	watcher->moveToThread(thread);
	connect(watcher, &RenderTicketWatcher::finished, this,
			&RenderTask::ticket_done, Qt::DirectConnection);
	running_watchers_.append(watcher);
}

void RenderTask::increment_running_tickets()
{
	finished_watcher_mutex_.lock();
	running_tickets_++;
	finished_watcher_mutex_.unlock();
}

void RenderTask::start_ticket(QThread *watcher_thread, ColorManager *manager,
							 const Rational &time, RenderMode::Mode mode,
							 FrameHashCache *cache, const QSize &force_size,
							 const QMatrix4x4 &force_matrix,
							 PixelFormat force_format, int force_channel_count,
							 ColorProcessorPtr force_color_output,
							 const ColorTransform &force_color_transform)
{
	RenderManager::RenderVideoParams rvp(viewer_->get_connected_texture_output(),
										 video_params_, audio_params_, time,
										 manager, mode);

	rvp.force_size = force_size;
	rvp.force_matrix = force_matrix;
	rvp.force_format = force_format;
	rvp.force_color_output = force_color_output;
	rvp.force_color_transform = force_color_transform;
	rvp.force_channel_count = force_channel_count;

	if (cache) {
		rvp.add_cache(cache);
	}

	RenderTicketWatcher *watcher = new RenderTicketWatcher();
	watcher->setProperty("time", QVariant::fromValue(time));
	prepare_watcher(watcher, watcher_thread);
	increment_running_tickets();
	watcher->set_ticket(RenderManager::instance()->render_frame(rvp));
}

void RenderTask::ticket_done(RenderTicketWatcher *watcher)
{
	finished_watcher_mutex_.lock();
	finished_watchers_.push_back(watcher);
	finished_watcher_wait_cond_.wakeAll();
	running_tickets_--;
	finished_watcher_mutex_.unlock();
}

}
