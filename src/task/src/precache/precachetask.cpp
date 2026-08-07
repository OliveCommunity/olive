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

#include <vector>

#include "node/factory.h"
#include "node/node.h"
#include "node/project.h"
#include "render/cache.h"
#include "node/track.h"
#include "rendermodes.h"
#include "timeline/workarea.h"

#include "nodehandle.h"

namespace olive
{

namespace
{

const char *k_viewer_output_id = "org.olivevideoeditor.Olive.vieweroutput";

} // namespace

PreCacheTask::PreCacheTask(OakNodeFootage footage, int index,
						   OakNodeSequence sequence)
	: project_({})
	, footage_({})
	, audio_params_(nullptr)
{
	// Set video and audio params
	OakVideoParams video_params = {};
	if (oaknode_sequence_get_video_params(sequence, 0, &video_params) ==
		OAKNODE_OK) {
		set_video_params(video_params);
		oakcommon_videoparams_free(&video_params);
	}
	oaknode_sequence_get_audio_params(sequence, 0, &audio_params_);
	set_audio_params(audio_params_);

	// Create new project
	project_ = oaknode_project_init();

	// Create viewer with same parameters as the sequence
	set_viewer(oaknode_factory_create_from_id(k_viewer_output_id));
	oaknode_project_add_node(project_, viewer());
	if (video_params.ctx) {
		oaknode_viewer_set_video_params(viewer(), &video_params);
	}
	if (audio_params_) {
		oaknode_viewer_set_audio_params(viewer(), audio_params_);
	}

	// Copy project config nodes
	OakNodeProject source_project = {};
	oaknode_node_get_project(oaknode_footage_as_node(footage),
							 &source_project);
	if (source_project.ctx) {
		oaknode_project_copy_settings(project_, source_project);
		oaknode_project_free(&source_project);
	}

	// Copy footage node so it can precache without any modifications from the user screwing it up
	OakNodeNode footage_copy =
		oaknode_node_create_copy(oaknode_footage_as_node(footage));
	oaknode_project_add_node(project_, footage_copy);
	oaknode_node_copy_inputs(footage_copy, oaknode_footage_as_node(footage),
							 0);

	// Borrowed footage alias of the copied node (releasing it only frees
	// the handle box; the graph owns the node).
	footage_ = oaknode_c_api::make_handle<OakNodeFootage>(
		oaknode_c_api::to_native<void>(footage_copy), false, nullptr);

	oaknode_node_connect(footage_copy, viewer(),
						 OAKNODE_SEQUENCE_TEXTURE_INPUT);
	oaknode_node_set_value_hint_track(viewer(),
									  OAKNODE_SEQUENCE_TEXTURE_INPUT,
									  OAKNODE_TRACK_TYPE_VIDEO, index);

	// The graph owns the copy now; release our handle box.
	oaknode_node_free(&footage_copy);

	char filename[1024];
	if (oaknode_footage_filename(footage, filename, sizeof(filename)) <=
		0) {
		filename[0] = 0;
	}
	set_title("Pre-caching " + std::string(filename) + ":" +
			  std::to_string(index));
}

PreCacheTask::~PreCacheTask()
{
	// This should delete the footage we copied and the viewer we created
	oaknode_project_free(&project_);
	// Release the borrowed alias box (the footage itself died with the
	// project above).
	oaknode_c_api::free_handle(&footage_);
	if (audio_params_) {
		oakcore_audioparams_free(audio_params_);
	}
}

bool PreCacheTask::run()
{
	// Get list of invalidated ranges
	TimeRange intersection;

	OakTimelineWorkArea workarea =
		oaktimeline_workarea_of(oaknode_footage_as_node(footage_));

	int64_t len_n = 0, len_d = 1;
	oaknode_footage_get_video_length(footage_, &len_n, &len_d);
	Rational video_length((int)len_n, (int)len_d);

	int wa_enabled = 0;
	int wa_in = 0, wa_ind = 1, wa_out = 0, wa_outd = 1;
	if (workarea.ctx) {
		oaktimeline_workarea_get(workarea, &wa_in, &wa_ind, &wa_out,
								 &wa_outd, &wa_enabled);
	}

	if (workarea.ctx && wa_enabled) {
		// If we're caching only in-out, limit the range to that
		intersection = TimeRange(Rational(wa_in, wa_ind),
								 Rational(wa_out, wa_outd));
	} else {
		// Otherwise use full length
		intersection = TimeRange(Rational(0), video_length);
	}

	// Addref'd handle: the cache itself stays owned by the viewer node,
	// this box is released at the end of the scope.
	OakRenderCache cache_handle = {};
	oaknode_node_get_video_frame_cache(viewer(), &cache_handle);

	int range_count = oakrender_cache_get_invalidated_ranges(
		cache_handle,
		intersection.in().numerator(), intersection.in().denominator(),
		intersection.out().numerator(), intersection.out().denominator(),
		nullptr, 0);

	TimeRangeList video_range;
	if (range_count > 0) {
		std::vector<int64_t> flat(size_t(range_count) * 4);
		oakrender_cache_get_invalidated_ranges(
			cache_handle,
			intersection.in().numerator(), intersection.in().denominator(),
			intersection.out().numerator(), intersection.out().denominator(),
			flat.data(), range_count);

		for (int i = 0; i < range_count; i++) {
			video_range.insert(TimeRange(
				Rational(int(flat[i * 4 + 0]), int(flat[i * 4 + 1])),
				Rational(int(flat[i * 4 + 2]), int(flat[i * 4 + 3]))));
		}
	}

	OakNodeColorManager color_manager =
		oaknode_colormanager_init(project_);

	render(color_manager, video_range, TimeRangeList(), TimeRange(),
		   0 /* RenderMode::k_online */, cache_handle, ForceParams());

	oaknode_colormanager_free(&color_manager);

	// Release the borrowed boxes (the objects stay with their owners).
	oaktimeline_workarea_free(&workarea);
	oakrender_cache_free(&cache_handle);

	return true;
}

bool PreCacheTask::frame_downloaded(OakCodecFrame frame,
									const Rational &time)
{
	// Do nothing. Pre-cache essentially just creates more frames in the cache, it doesn't need to do
	// anything else.
	(void)frame;
	(void)time;

	return true;
}

bool PreCacheTask::audio_downloaded(const TimeRange &range,
									OakSampleBuffer *samples)
{
	// Pre-cache doesn't cache any audio
	(void)range;
	(void)samples;

	return true;
}

}
