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

#include "export.h"

#include <cstring>
#include <filesystem>

#include <cstdint>

#include "common/filefunctions.h"
#include "node/sequence.h"
#include "olive/core/util/timecodefunctions.h"
#include "render/color.h"

#include "nodehandle.h"

namespace olive
{

namespace
{

std::string node_label(OakNodeNode node)
{
	int needed = oaknode_node_get_label(node, nullptr, 0);
	if (needed <= 0) {
		return std::string();
	}
	std::string label(size_t(needed), 0);
	oaknode_node_get_label(node, label.data(), needed);
	label.resize(size_t(needed) - 1);
	return label;
}

std::string encoder_error(OakEncoder encoder)
{
	char buf[512];
	if (oakcodec_encoder_last_error(encoder, buf, sizeof(buf)) <= 0) {
		return std::string();
	}
	return buf;
}

/**
 * @brief Copy an oakrender frame into an oakcodec frame (pixel copy;
 *        the two handles wrap different control blocks).
 *
 * Returns an empty OakFrame (ctx == NULL) on failure.
 */
OakFrame copy_frame_to_codec(OakCodecFrame render_frame)
{
	OakFrame empty = {};
	if (!render_frame.ctx) {
		return empty;
	}

	oakrender_video_params rp = {};
	if (oakrender_codec_frame_get_params(render_frame, &rp) !=
		OAKRENDER_OK) {
		return empty;
	}

	OakVideoParams vp = oakcommon_videoparams_init();
	if (!vp.ctx) {
		return empty;
	}
	oakcommon_videoparams_set_width(vp, rp.width);
	oakcommon_videoparams_set_height(vp, rp.height);
	oakcommon_videoparams_set_time_base(vp, rp.time_base_num,
										rp.time_base_den);
	oakcommon_videoparams_set_frame_rate(vp, rp.time_base_den,
										 rp.time_base_num);
	oakcommon_videoparams_set_format(vp, rp.format);
	oakcommon_videoparams_set_pixel_aspect_ratio(vp, rp.pixel_aspect_num,
												 rp.pixel_aspect_den);
	oakcommon_videoparams_set_interlacing(vp, rp.interlacing);
	oakcommon_videoparams_set_color_range(vp, rp.color_range);
	oakcommon_videoparams_set_divider(vp, rp.divider);
	oakcommon_videoparams_set_video_type(vp, rp.video_type);
	oakcommon_videoparams_set_premultiplied_alpha(vp,
												  rp.premultiplied_alpha);

	OakFrame out = oakcodec_frame_init_with_params(vp);
	oakcommon_videoparams_free(&vp);
	if (!out.ctx) {
		return empty;
	}

	if (oakcodec_frame_allocate(out) != OAKCODEC_OK) {
		oakcodec_frame_free(&out);
		return empty;
	}

	// Copy scanlines
	const uint8_t *src =
		static_cast<const uint8_t *>(oakrender_codec_frame_const_data(render_frame));
	uint8_t *dst = static_cast<uint8_t *>(oakcodec_frame_data(out));
	int src_linesize = oakrender_codec_frame_linesize_bytes(render_frame);
	int dst_linesize = oakcodec_frame_linesize_bytes(out);
	int copy_bytes = src_linesize < dst_linesize ? src_linesize
												 : dst_linesize;
	if (!src || !dst || copy_bytes <= 0) {
		oakcodec_frame_free(&out);
		return empty;
	}
	for (int y = 0; y < rp.height; y++) {
		memcpy(dst + size_t(y) * dst_linesize,
			   src + size_t(y) * src_linesize, size_t(copy_bytes));
	}

	return out;
}

/**
 * @brief Borrowed sequence alias of a viewer node handle (same underlying
 *        node; releasing the alias only frees its handle box).
 */
OakNodeSequence sequence_alias_of(OakNodeNode node)
{
	return oaknode_c_api::make_handle<OakNodeSequence>(
		oaknode_c_api::to_native<void>(node), false, nullptr);
}

} // namespace

ExportTask::ExportTask(OakNodeNode viewer_node,
					   OakNodeColorManager color_manager,
					   const oakcodec_encoding_params &params)
	: color_manager_({})
	, params_(params)
	, encoder_({})
	, subtitle_encoder_({})
	, frame_time_(0)
	, null_frame_streak_(0)
	, audio_time_(0)
{
	(void)color_manager;

	// Create a copy of the project
	OakNodeProject source_project = {};
	oaknode_node_get_project(viewer_node, &source_project);

	copier_ = oakrender_project_copier_create();
	if (copier_.ctx && source_project.ctx) {
		oakrender_project_copier_set_project(copier_, source_project);
	}
	oaknode_project_free(&source_project);

	set_viewer(oakrender_project_copier_get_copy(copier_, viewer_node));

	OakNodeProject copied_project =
		oakrender_project_copier_get_copied_project(copier_);
	color_manager_ = oaknode_colormanager_init(copied_project);
	oaknode_project_free(&copied_project);

	// Adjust video params to have no divider
	OakNodeSequence viewer_sequence = sequence_alias_of(viewer_node);
	OakVideoParams vp = {};
	oaknode_sequence_get_video_params(viewer_sequence, 0, &vp);
	oakcommon_videoparams_set_divider(vp, 1);
	oakcommon_videoparams_set_time_base(vp, params_.video_time_base_num,
										params_.video_time_base_den);
	oakcommon_videoparams_set_frame_rate(vp, params_.video_time_base_den,
										 params_.video_time_base_num);
	set_video_params(vp);
	oakcommon_videoparams_free(&vp);

	OakAudioParams *audio_params = nullptr;
	oaknode_sequence_get_audio_params(viewer_sequence, 0, &audio_params);
	set_audio_params(audio_params);
	oaknode_sequence_free(&viewer_sequence);

	set_title("Exporting \"" + node_label(viewer_node) + "\"");
	set_native_progress_signalling_enabled(false);
}

ExportTask::~ExportTask()
{
	if (encoder_.ctx) {
		oakcodec_encoder_free(&encoder_);
	}
	if (subtitle_encoder_.ctx) {
		oakcodec_encoder_free(&subtitle_encoder_);
	}
	oakrender_color_processor_free(&color_processor_);
	oaknode_colormanager_free(&color_manager_);
	oakrender_project_copier_free(&copier_);
	if (audio_params()) {
		oakcore_audioparams_free(audio_params());
	}
}

bool ExportTask::run()
{
	// For safety, if we're overwriting, we save to a temporary filename and then only overwrite it
	// at the end
	std::string real_filename = params_.filename;
	OakFileFunctions filefuncs = oakcommon_filefunctions_init();
	if (std::filesystem::exists(real_filename)) {
		// Generate a filename that definitely doesn't exist
		char buf[1024];
		if (oakcommon_filefunctions_get_safe_temporary_filename(
				filefuncs, real_filename.c_str(), buf, sizeof(buf)) > 0) {
			strncpy(params_.filename, buf, sizeof(params_.filename) - 1);
			params_.filename[sizeof(params_.filename) - 1] = 0;
		}
	}

	// If we're exporting to a sidecar subtitle file, disable the subtitles in the main encoder
	bool subtitles_enabled = params_.subtitles_enabled != 0;
	oakcodec_encoding_params sidecar_params = params_;
	if (subtitles_enabled && params_.subtitles_are_sidecar) {
		params_.subtitles_enabled = 0;
	}

	encoder_ = oakcodec_encoder_init(&params_);
	if (!encoder_.ctx) {
		set_error("Failed to create encoder");
		return false;
	}

	if (oakcodec_encoder_open(encoder_) != OAKCODEC_OK) {
		set_error("Failed to open file: " + encoder_error(encoder_));
		return false;
	}

	if (subtitles_enabled && params_.subtitles_are_sidecar) {
		// Construct sidecar params
		sidecar_params.video_enabled = 0;
		sidecar_params.audio_enabled = 0;

		std::filesystem::path fi(real_filename);
		std::string sidecar_filename = fi.stem().string();
		char ext[64];
		if (oakcodec_export_format_get_extension(
				sidecar_params.subtitles_sidecar_format, ext,
				sizeof(ext)) > 0) {
			sidecar_filename += ".";
			sidecar_filename += ext;
		}
		sidecar_filename =
			(fi.parent_path() / sidecar_filename).string();

		strncpy(sidecar_params.filename, sidecar_filename.c_str(),
				sizeof(sidecar_params.filename) - 1);

		sidecar_params.format = sidecar_params.subtitles_sidecar_format;
		subtitle_encoder_ = oakcodec_encoder_init(&sidecar_params);
		if (!subtitle_encoder_.ctx) {
			set_error("Failed to create subtitle encoder");
			return false;
		}

		if (oakcodec_encoder_open(subtitle_encoder_) != OAKCODEC_OK) {
			set_error("Failed to open subtitle sidecar file: " +
					  sidecar_filename);
			return false;
		}
	} else {
		subtitle_encoder_ = encoder_;
	}

	if (params_.has_custom_range) {
		// Render custom range only
		export_range_ = TimeRange(
			Rational(int(params_.custom_range_in_num),
					 int(params_.custom_range_in_den)),
			Rational(int(params_.custom_range_out_num),
					 int(params_.custom_range_out_den)));
	} else {
		// Render entire sequence
		int len_n = 0, len_d = 1;
		OakNodeSequence viewer_sequence = sequence_alias_of(viewer());
		oaknode_sequence_get_length(viewer_sequence, &len_n, &len_d);
		oaknode_sequence_free(&viewer_sequence);
		export_range_ =
			TimeRange(Rational(0), Rational(len_n, len_d));
	}

	frame_time_ = 0;

	ForceParams force;

	if (params_.video_enabled) {
		// If a transformation matrix is applied to this video, create it here
		int src_w = 0, src_h = 0;
		oakcommon_videoparams_get_width(video_params(), &src_w);
		oakcommon_videoparams_get_height(video_params(), &src_h);

		if (src_w != params_.video_width || src_h != params_.video_height) {
			force.width = params_.video_width;
			force.height = params_.video_height;

			if (params_.video_scaling_method != 0 /* k_stretch */) {
				force.has_matrix = true;
				oakcodec_encoding_generate_matrix(
					params_.video_scaling_method, src_w, src_h,
					params_.video_width, params_.video_height,
					force.matrix);
			}
		} else {
			// Disables forcing size in the renderer
			force.width = 0;
			force.height = 0;
		}

		// Create color processor
		char reference_space[256];
		if (oaknode_colormanager_get_reference_color_space(
				color_manager_, reference_space,
				sizeof(reference_space)) > 0) {
			color_processor_ = oakrender_color_processor_create(
				reference_space, params_.color_transform_output, 0);
		}

		force.format =
			oakcodec_encoder_get_desired_pixel_format(encoder_);
		force.channel_count = 4; /* RGBA */
		force.color_output = color_processor_;
	}

	// Start render process
	TimeRangeList video_range, audio_range;
	TimeRange subtitle_range;

	if (params_.video_enabled) {
		if (export_range_.in() > 0) {
			int tb_num = 0, tb_den = 1;
			oakcommon_videoparams_frame_rate_as_time_base(video_params(),
														  &tb_num, &tb_den);
			export_range_.set_in(Timecode::snap_time_to_timebase(
				export_range_.in(), Rational(tb_num, tb_den)));
		}

		video_range = { export_range_ };
	}

	if (params_.audio_enabled) {
		audio_range = { export_range_ };
	}

	if (subtitles_enabled) {
		subtitle_range = export_range_;
	}

	render(color_manager_, video_range, audio_range, subtitle_range,
		   0 /* RenderMode::k_online */, {}, force);

	bool success = true;

	oakcodec_encoder_flush(encoder_);
	std::string err = encoder_error(encoder_);
	if (!err.empty()) {
		set_error(err);
		success = false;
	}

	if (subtitle_encoder_.ctx != encoder_.ctx) {
		oakcodec_encoder_flush(subtitle_encoder_);
		err = encoder_error(subtitle_encoder_);
		if (!err.empty()) {
			set_error(err);
			success = false;
		}
	}

	// If cancelled, delete the file we made, which is always a file we created since we write to a
	// temp file during the actual encoding process
	if (is_cancelled()) {
		std::error_code ec;
		std::filesystem::remove(params_.filename, ec);
	} else if (real_filename != params_.filename) {
		// If we were writing to a temp file, overwrite now
		int renamed = 0;
		if (oakcommon_filefunctions_rename_file_allow_overwrite(
				filefuncs, params_.filename, real_filename.c_str(),
				&renamed) != OAKCOMMON_OK ||
			!renamed) {
			set_error("Failed to overwrite \"" + real_filename +
					  "\". Export has been saved as \"" +
					  std::string(params_.filename) + "\" instead.");
			success = false;
		}
	}

	oakcommon_filefunctions_free(&filefuncs);

	return success;
}

bool ExportTask::frame_downloaded(OakCodecFrame f, const Rational &time)
{
	// The worker pool finishes tickets without a result when no worker is
	// available (or every worker crashed). Grinding through the whole
	// timeline at several seconds per dead worker looks like a hang, so
	// fail the export after a short streak of missing frames.
	if (!f.ctx) {
		if (++null_frame_streak_ >= 8) {
			set_error("Render workers failed to deliver " +
					  std::to_string(null_frame_streak_) +
					  " consecutive frames; aborting export");
			return false;
		}
	} else {
		null_frame_streak_ = 0;
	}

	Rational actual_time = time - export_range_.in();

	time_map_.insert({ actual_time, f });

	while (!is_cancelled()) {
		int tb_num = 0, tb_den = 1;
		oakcommon_videoparams_frame_rate_as_time_base(video_params(),
													  &tb_num, &tb_den);
		Rational real_time = Timecode::timestamp_to_time(
			frame_time_, Rational(tb_num, tb_den));

		auto it = time_map_.find(real_time);
		if (it == time_map_.end()) {
			break;
		}

		// Unfortunately this can't be done in another thread since the frames need to be sent
		// one after the other chronologically.
		OakFrame codec_frame = copy_frame_to_codec(it->second);
		bool written = codec_frame.ctx &&
					   oakcodec_encoder_write_video(encoder_, codec_frame) ==
						   OAKCODEC_OK;
		if (codec_frame.ctx) {
			oakcodec_frame_free(&codec_frame);
		}
		if (!written) {
			set_error(encoder_error(encoder_));
			return false;
		}
		if (it->second.ctx) {
			oakrender_codec_frame_free(&it->second);
		}
		time_map_.erase(it);

		frame_time_++;
		emit_progress(double(frame_time_) /
					  double(get_total_number_of_frames()));
	}

	return true;
}

bool ExportTask::audio_downloaded(const TimeRange &range,
								  OakSampleBuffer *samples)
{
	TimeRange adjusted_range = range - export_range_.in();

	if (adjusted_range.in() == audio_time_) {
		if (!write_audio_loop(adjusted_range, samples)) {
			return false;
		}
	} else {
		audio_map_.insert({ adjusted_range, samples });
	}

	return true;
}

bool ExportTask::encode_subtitle(OakNodeBlock sub)
{
	// The subtitle block's text is its standard "text" input
	char text[8192];
	if (oaknode_node_get_input_string(oaknode_block_as_node(sub), "text",
									  text, sizeof(text)) < 0) {
		text[0] = 0;
	}

	int in_n = 0, in_d = 1, out_n = 0, out_d = 1;
	oaknode_block_get_in(sub, &in_n, &in_d);
	oaknode_block_get_out(sub, &out_n, &out_d);

	double in_seconds = in_d ? double(in_n) / in_d : 0.0;
	double out_seconds = out_d ? double(out_n) / out_d : 0.0;
	if (oakcodec_encoder_write_subtitle(subtitle_encoder_, text,
										in_seconds, out_seconds) !=
		OAKCODEC_OK) {
		set_error(encoder_error(subtitle_encoder_));
		return false;
	}
	return true;
}

bool ExportTask::write_audio_loop(const TimeRange &time,
								  OakSampleBuffer *samples)
{
	int channels = oakcore_samplebuffer_channel_count(samples);
	size_t sample_count = oakcore_samplebuffer_sample_count(samples);
	std::vector<float> interleaved(sample_count * (size_t)channels);
	// SampleBuffer is planar; interleave for the encoder
	std::vector<const float *> planar((size_t)channels);
	oakcore_samplebuffer_to_raw_ptrs(samples,
									 const_cast<float **>(planar.data()));
	for (size_t i = 0; i < sample_count; i++) {
		for (int c = 0; c < channels; c++) {
			interleaved[i * size_t(channels) + size_t(c)] =
				planar[size_t(c)][i];
		}
	}

	if (oakcodec_encoder_write_audio(encoder_, interleaved.data(),
									 int(sample_count)) != OAKCODEC_OK) {
		set_error(encoder_error(encoder_));
		return false;
	}

	audio_time_ = time.out();

	for (auto it = audio_map_.begin(); it != audio_map_.end(); it++) {
		TimeRange t = it->first;
		OakSampleBuffer *s = it->second;

		if (t.in() == audio_time_) {
			// Erase from audio map since we're just about to write it
			audio_map_.erase(it);

			// Call recursively to write the next sample buffer
			if (!write_audio_loop(t, s)) {
				return false;
			}

			// Break out of loop
			break;
		}
	}

	return true;
}

}
