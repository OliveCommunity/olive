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

#ifndef OAK_RENDERTASK_H
#define OAK_RENDERTASK_H

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

#include <olive/core/core.h>

#include "common/colortransform.h"
#include "common/videoparams.h"
#include "node/block.h"
#include "node/colormanager.h"
#include "node/node.h"
#include "render/ticket.h"
#include "task.h"

namespace olive
{

using namespace core;

/**
 * @brief Task that renders a set of video/audio ranges through
 *        oakrender tickets
 *
 * De-Qt version: drives the oakrender ticket C API; the finished
 * callback is the ticket's return channel. The two-step
 * texture-then-download rendering distinction is gone (tickets always
 * produce frames), so every video ticket counts full progress.
 */
class RenderTask : public Task {
public:
	RenderTask();

	virtual ~RenderTask() override;

protected:
	struct ForceParams {
		int width = 0; /**< 0/0 = off */
		int height = 0;
		double matrix[16] = { 0 };
		bool has_matrix = false;
		int format = -1; /**< PixelFormat as int, -1 = off */
		int channel_count = 0; /**< 0 = off */
		OakColorProcessor color_output = {}; /**< borrowed; empty ctx = none */
		OakColorTransform color_transform = {}; /**< empty ctx = default */
	};

	bool render(OakNodeColorManager manager,
				const TimeRangeList &video_range,
				const TimeRangeList &audio_range,
				const TimeRange &subtitle_range, int render_mode,
				OakNodeFrameCache *cache,
				const ForceParams &force);

	virtual bool download_frame(OakCodecFrame frame, const Rational &time);

	virtual bool frame_downloaded(OakCodecFrame frame,
								  const Rational &time) = 0;

	virtual bool audio_downloaded(const TimeRange &range,
								  OakSampleBuffer *samples) = 0;

	virtual bool encode_subtitle(OakNodeBlock subtitle);

	OakNodeNode viewer() const
	{
		return viewer_;
	}

	void set_viewer(OakNodeNode v)
	{
		viewer_ = v;
	}

	OakVideoParams video_params() const
	{
		return video_params_;
	}

	void set_video_params(const OakVideoParams &video_params)
	{
		if (video_params_.ctx == video_params.ctx && video_params_.ctx) {
			return;
		}
		if (video_params_.ctx) {
			oakcommon_videoparams_free(&video_params_);
		}
		video_params_ = video_params;
		if (video_params_.ctx) {
			video_params_.addref(video_params_.ctx);
		}
	}

	OakAudioParams *audio_params() const
	{
		return audio_params_;
	}

	void set_audio_params(OakAudioParams *audio_params)
	{
		audio_params_ = audio_params;
	}

	/**
	 * @brief Kept for API parity; with ticket-based rendering every video
	 *        ticket produces a frame, so the two-step texture/download
	 *        split no longer exists and this is always false.
	 */
	virtual bool two_step_frame_rendering() const
	{
		return false;
	}

	virtual void cancel_event() override
	{
		finished_mutex_.lock();
		finished_wait_cond_.notify_all();
		finished_mutex_.unlock();
	}

	void set_native_progress_signalling_enabled(bool e)
	{
		native_progress_signalling_ = e;
	}

	/**
	 * @brief Only valid after render() is called
	 */
	int64_t get_total_number_of_frames() const
	{
		return total_number_of_frames_;
	}

private:
	struct VideoTicketRequest {
		Rational time;
	};

	void on_ticket_finished(OakRenderTicket ticket);

	bool start_video_ticket(OakNodeColorManager manager,
							const Rational &time, int mode,
							OakNodeFrameCache *cache,
							const ForceParams &force);

	OakNodeNode viewer_;

	OakVideoParams video_params_;

	OakAudioParams *audio_params_;

	std::mutex finished_mutex_;
	std::condition_variable finished_wait_cond_;
	std::deque<OakRenderTicket> finished_tickets_;
	std::vector<OakRenderTicket> running_ticket_list_;
	int running_tickets_;

	bool native_progress_signalling_;

	int64_t total_number_of_frames_;
};

}

#endif // OAK_RENDERTASK_H
