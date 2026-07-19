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

#include "precachetask.h"

#include "node/project.h"

namespace olive
{

PreCacheTask::PreCacheTask(Footage *footage, int index, Sequence *sequence)
{
	// Set video and audio params
	set_video_params(sequence->get_video_params());
	set_audio_params(sequence->get_audio_params());

	// Create new project
	project_ = new Project();

	// Create viewer with same parameters as the sequence
	set_viewer(new ViewerOutput());
	viewer()->setParent(project_);
	viewer()->set_video_params(sequence->get_video_params());
	viewer()->set_audio_params(sequence->get_audio_params());

	// Copy project config nodes
	Project::copy_settings(footage->project(), project_);

	// Copy footage node so it can precache without any modifications from the user screwing it up
	footage_ = static_cast<Footage *>(footage->copy());
	footage_->setParent(project_);
	Node::copy_inputs(footage, footage_, false);

	Node::connect_edge(footage_,
					  NodeInput(viewer(), ViewerOutput::k_texture_input));
	viewer()->set_value_hint_for_input(
		ViewerOutput::k_texture_input,
		Node::ValueHint({ NodeValue::k_texture },
						Track::Reference(Track::k_video, index).to_string()));

	set_title(tr("Pre-caching %1:%2")
				 .arg(footage_->filename(), QString::number(index)));
}

PreCacheTask::~PreCacheTask()
{
	// This should delete the footage we copied and the viewer we created
	delete project_;
}

bool PreCacheTask::run()
{
	// Get list of invalidated ranges
	TimeRange intersection;

	if (footage_->get_work_area()->enabled()) {
		// If we're caching only in-out, limit the range to that
		intersection = footage_->get_work_area()->range();
	} else {
		// Otherwise use full length
		intersection = TimeRange(0, footage_->get_video_length());
	}

	TimeRangeList video_range =
		viewer()->video_frame_cache()->get_invalidated_ranges(intersection);

	render(project_->color_manager(), video_range, TimeRangeList(), TimeRange(),
		   RenderMode::k_online, viewer()->video_frame_cache());

	return true;
}

bool PreCacheTask::frame_downloaded(FramePtr frame, const Rational &time)
{
	// Do nothing. Pre-cache essentially just creates more frames in the cache, it doesn't need to do
	// anything else.

	Q_UNUSED(frame)
	Q_UNUSED(time)

	return true;
}

bool PreCacheTask::audio_downloaded(const TimeRange &range,
								   const SampleBuffer &samples)
{
	// Pre-cache doesn't cache any audio

	Q_UNUSED(range)
	Q_UNUSED(samples)

	return true;
}

}
