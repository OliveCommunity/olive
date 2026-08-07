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

#include <thread>

#include "node/block.h"
#include "node/sequence.h"
#include "node/track.h"

#include "nodehandle.h"

namespace olive
{

namespace
{

Rational task_block_in(OakNodeBlock b)
{
	int n = 0, d = 1;
	oaknode_block_get_in(b, &n, &d);
	return Rational(n, d);
}

Rational task_block_out(OakNodeBlock b)
{
	int n = 0, d = 1;
	oaknode_block_get_out(b, &n, &d);
	return Rational(n, d);
}

} // namespace

RenderTask::RenderTask()
	: viewer_({})
	, video_params_({})
	, audio_params_(nullptr)
	, running_tickets_(0)
	, native_progress_signalling_(true)
	, total_number_of_frames_(0)
{
}

RenderTask::~RenderTask()
{
	// Borrowed/owned indifferent: releasing the viewer handle only frees
	// the handle box once the node lives in a graph.
	oaknode_node_free(&viewer_);
	if (video_params_.ctx) {
		oakcommon_videoparams_free(&video_params_);
	}
}

void RenderTask::on_ticket_finished(OakRenderTicket ticket)
{
	finished_mutex_.lock();
	finished_tickets_.push_back(ticket);
	finished_wait_cond_.notify_all();
	running_tickets_--;
	finished_mutex_.unlock();
}

bool RenderTask::start_video_ticket(OakNodeColorManager manager,
									const Rational &time, int mode,
									OakNodeFrameCache *cache,
									const ForceParams &force)
{
	OakNodeNode output_node = {};
	oaknode_node_input_get_connected_node(
		viewer_, OAKNODE_SEQUENCE_TEXTURE_INPUT, &output_node);
	if (!output_node.ctx) {
		return false;
	}

	oakrender_video_ticket_params params = {};
	params.output_node = output_node;
	params.video_params = video_params_;
	params.audio_params = audio_params_;
	params.time_num = time.numerator();
	params.time_den = time.denominator();
	params.color_manager = manager;
	params.mode = mode;
	params.force_width = force.width;
	params.force_height = force.height;
	params.has_force_matrix = force.has_matrix ? 1 : 0;
	for (int i = 0; i < 16; i++) {
		params.force_matrix[i] = force.matrix[i];
	}
	params.force_format = force.format;
	params.force_channel_count = force.channel_count;
	params.force_color_output = force.color_output;
	params.force_color_transform = force.color_transform;
	params.cache = cache;

	OakRenderTicket ticket = oakrender_ticket_render_frame(
		&params,
		[](OakRenderTicket t, void *userdata) {
			static_cast<RenderTask *>(userdata)->on_ticket_finished(t);
		},
		this);
	// Per-frame call: release the borrowed handle box (the ticket keeps
	// the native node, not the handle).
	oaknode_node_free(&output_node);
	if (!ticket.ctx) {
		return false;
	}

	finished_mutex_.lock();
	running_ticket_list_.push_back(ticket);
	running_tickets_++;
	finished_mutex_.unlock();
	return true;
}

bool RenderTask::render(OakNodeColorManager manager,
						const TimeRangeList &video_range,
						const TimeRangeList &audio_range,
						const TimeRange &subtitle_range, int render_mode,
						OakNodeFrameCache *cache, const ForceParams &force)
{
	oakrender_manager_set_aggressive_gc(1);

	double progress_counter = 0;
	double total_length = 0;

	// Queue audio jobs
	for (const TimeRange &range : audio_range) {
		OakNodeNode output_node = {};
		oaknode_node_input_get_connected_node(
			viewer_, OAKNODE_SEQUENCE_SAMPLES_INPUT, &output_node);
		if (!output_node.ctx) {
			continue;
		}

		OakRenderTicket ticket = oakrender_ticket_render_audio(
			output_node, range.in().numerator(), range.in().denominator(),
			range.out().numerator(), range.out().denominator(),
			audio_params_, render_mode,
			[](OakRenderTicket t, void *userdata) {
				static_cast<RenderTask *>(userdata)->on_ticket_finished(t);
			},
			this);
		if (ticket.ctx) {
			finished_mutex_.lock();
			running_ticket_list_.push_back(ticket);
			running_tickets_++;
			finished_mutex_.unlock();
		}
		oaknode_node_free(&output_node);
	}

	// Frame timestamps
	Rational timebase;
	{
		int tb_num = 0, tb_den = 1;
		oakcommon_videoparams_frame_rate_as_time_base(video_params_,
													  &tb_num, &tb_den);
		timebase = Rational(tb_num, tb_den);
	}

	std::vector<Rational> frame_times;
	for (const TimeRange &range : video_range) {
		for (Rational t = range.in(); t < range.out(); t += timebase) {
			frame_times.push_back(t);
		}
	}
	total_number_of_frames_ = int64_t(frame_times.size());
	total_length += double(total_number_of_frames_);
	if (total_length <= 0) {
		total_length = 1;
	}

	// Start a limited number of renders, then start one more for each
	// that finishes, so rendered frames don't stack up in memory
	const int maximum_rendered_frames =
		std::max(1, int(std::thread::hardware_concurrency()));

	size_t next_frame_index = 0;
	for (int i = 0;
		 i < maximum_rendered_frames && next_frame_index < frame_times.size();
		 i++, next_frame_index++) {
		start_video_ticket(manager, frame_times[next_frame_index],
						   render_mode, cache, force);
	}

	bool result = true;

	// Subtitle loop, loops over all blocks in sequence on all tracks
	if (!subtitle_range.length().isNull()) {
		// Borrowed sequence alias of the viewer handle (same underlying
		// node; releasing it only frees the handle box).
		OakNodeSequence sequence = oaknode_c_api::make_handle<
			OakNodeSequence>(oaknode_c_api::to_native<void>(viewer_), false,
							 nullptr);
		OakNodeTrackList list = {};
		oaknode_sequence_get_track_list(
			sequence, OAKNODE_TRACK_TYPE_SUBTITLE, &list);

		if (list.ctx) {
			int track_count = 0;
			oaknode_tracklist_get_track_count(list, &track_count);

			std::vector<int> block_indexes(size_t(track_count), 0);
			std::vector<int> tracks_to_push;
			do {
				tracks_to_push.clear();

				for (int i = 0; i < track_count; i++) {
					OakNodeTrack this_track = {};
					oaknode_tracklist_get_track_at(list, i, &this_track);
					if (!this_track.ctx) {
						continue;
					}

					int muted = 0;
					oaknode_track_get_muted(this_track, &muted);
					if (muted) {
						continue;
					}

					int this_block_count = 0;
					oaknode_track_get_block_count(this_track,
												  &this_block_count);
					int &this_block_index = block_indexes[size_t(i)];
					if (this_block_index >= this_block_count) {
						continue;
					}

					OakNodeBlock this_block = {};
					oaknode_track_get_block_at(this_track,
											   this_block_index,
											   &this_block);

					OakNodeTrack compare_track = {};
					if (!tracks_to_push.empty()) {
						oaknode_tracklist_get_track_at(
							list, tracks_to_push.front(), &compare_track);
					}
					OakNodeBlock compare_block = {};
					if (compare_track.ctx) {
						oaknode_track_get_block_at(
							compare_track,
							block_indexes[size_t(
								tracks_to_push.front())],
							&compare_block);
					}
					if (!compare_track.ctx ||
						task_block_out(compare_block) >= task_block_in(this_block)) {
						if (compare_track.ctx &&
							task_block_in(compare_block) !=
								task_block_in(this_block)) {
							tracks_to_push.clear();
						}
						tracks_to_push.push_back(i);
					}
				}

				for (int i : tracks_to_push) {
					OakNodeTrack this_track = {};
					oaknode_tracklist_get_track_at(list, i, &this_track);
					OakNodeBlock this_block = {};
					oaknode_track_get_block_at(
						this_track, block_indexes[size_t(i)], &this_block);

					int kind = OAKNODE_BLOCK_OTHER;
					if (this_block.ctx) {
						oaknode_block_get_kind(this_block, &kind);
					}
					if (this_block.ctx && kind != OAKNODE_BLOCK_GAP) {
						int enabled = 0;
						oaknode_block_get_enabled(this_block, &enabled);
						if (enabled) {
							if (!encode_subtitle(this_block)) {
								result = false;
								break;
							}
						}
					}

					block_indexes[size_t(i)]++;
				}
			} while (!tracks_to_push.empty());
		}

		oaknode_sequence_free(&sequence);
	}

	std::unique_lock<std::mutex> loop_lock(finished_mutex_);

	while (result && !is_cancelled()) {
		while (!finished_tickets_.empty() && !is_cancelled() && result) {
			OakRenderTicket ticket = finished_tickets_.front();
			finished_tickets_.pop_front();

			loop_lock.unlock();

			int type = oakrender_ticket_get_type(ticket);

			if (type == OAKRENDER_TICKET_AUDIO) {
				Rational range_in;
				Rational range_out;
				{
					int64_t rn = 0, rd = 1, ro_n = 0, ro_d = 1;
					oakrender_ticket_get_range(ticket, &rn, &rd, &ro_n,
											   &ro_d);
					range_in = Rational((int)rn, (int)rd);
					range_out = Rational((int)ro_n, (int)ro_d);
				}
				TimeRange range(range_in, range_out);

				OakSampleBuffer *samples = nullptr;
				if (oakrender_ticket_get_samples(ticket, &samples) ==
					OAKRENDER_OK) {
					if (!audio_downloaded(range, samples)) {
						result = false;
					}
					oakcore_samplebuffer_free(samples);
				} else {
					result = false;
				}

			} else {
				int64_t tn, td;
				oakrender_ticket_get_time(ticket, &tn, &td);
				Rational time((int)tn, (int)td);

				OakCodecFrame frame = {};
				oakrender_ticket_get_frame(ticket, &frame);

				if (two_step_frame_rendering() &&
					!download_frame(frame, time)) {
					result = false;
				} else if (!frame_downloaded(frame, time)) {
					result = false;
				}

				if (native_progress_signalling_) {
					double progress_to_add = 1.0;
					if (two_step_frame_rendering()) {
						progress_to_add *= 0.5;
					}
					progress_counter += progress_to_add;
					emit_progress(progress_counter / total_length);
				}

				if (frame.ctx) {
					oakrender_codec_frame_free(&frame);
				}

				if (next_frame_index < frame_times.size()) {
					start_video_ticket(manager,
									   frame_times[next_frame_index++],
									   render_mode, cache, force);
				}
			}

			oakrender_ticket_free(&ticket);
			loop_lock.lock();
		}

		if (is_cancelled() || !result) {
			break;
		}

		if (running_tickets_ > 0) {
			finished_wait_cond_.wait(loop_lock);
		} else {
			break;
		}
	}

	loop_lock.unlock();

	if (is_cancelled() || !result) {
		// Cancel every ticket we created
		for (OakRenderTicket &ticket : running_ticket_list_) {
			oakrender_ticket_cancel(ticket);
			oakrender_ticket_wait(ticket);
			oakrender_ticket_free(&ticket);
		}
	}

	oakrender_manager_set_aggressive_gc(0);

	return result;
}

bool RenderTask::download_frame(OakCodecFrame frame, const Rational &time)
{
	(void)frame;
	(void)time;

	// NOTE: Doesn't reflect the actual return result of SaveFrameToCache
	return true;
}

bool RenderTask::encode_subtitle(OakNodeBlock subtitle)
{
	(void)subtitle;
	return true;
}

}
