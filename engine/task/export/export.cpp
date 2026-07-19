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

#include "node/color/colormanager/colormanager.h"

namespace olive
{

ExportTask::ExportTask(ViewerOutput *viewer_node, ColorManager *color_manager,
					   const EncodingParams &params)
	: params_(params)
{
	// Create a copy of the project
	copier_ = new ProjectCopier(this);
	copier_->set_project(viewer_node->project());

	set_viewer(copier_->get_copy(viewer_node));
	color_manager_ = copier_->get_copied_project()->color_manager();

	// Adjust video params to have no divider
	VideoParams vp = viewer_node->get_video_params();
	vp.set_divider(1);
	vp.set_time_base(params.video_params().time_base());
	vp.set_frame_rate(params.video_params().frame_rate());
	set_video_params(vp);

	set_audio_params(viewer_node->get_audio_params());

	set_title(tr("Exporting \"%1\"").arg(viewer_node->get_label()));
	set_native_progress_signalling_enabled(false);
}

bool ExportTask::run()
{
	// For safety, if we're overwriting, we save to a temporary filename and then only overwrite it
	// at the end
	QString real_filename = params_.filename();
	if (QFileInfo::exists(params_.filename())) {
		// Generate a filename that definitely doesn't exist
		params_.set_filename(
			FileFunctions::get_safe_temporary_filename(real_filename));
	}

	// If we're exporting to a sidecar subtitle file, disable the subtitles in the main encoder
	bool subtitles_enabled = params_.subtitles_enabled();
	EncodingParams sidecar_params = params_;
	if (subtitles_enabled && params_.subtitles_are_sidecar()) {
		params_.disable_subtitles();
	}

	encoder_ = std::shared_ptr<Encoder>(Encoder::create_from_params(params_));

	if (!encoder_) {
		set_error(tr("Failed to create encoder"));
		return false;
	}

	if (!encoder_->open()) {
		set_error(tr("Failed to open file: %1").arg(encoder_->get_error()));
		return false;
	}

	if (subtitles_enabled && params_.subtitles_are_sidecar()) {
		// Construct sidecar params
		sidecar_params.disable_video();
		sidecar_params.disable_audio();

		QString sidecar_filename;
		{
			QFileInfo fi(real_filename);
			sidecar_filename = fi.completeBaseName();
			sidecar_filename.append('.');
			sidecar_filename.append(ExportFormat::get_extension(
				sidecar_params.subtitle_sidecar_fmt()));
			sidecar_filename = fi.dir().filePath(sidecar_filename);
		}
		sidecar_params.set_filename(sidecar_filename);

		subtitle_encoder_ = std::shared_ptr<Encoder>(Encoder::create_from_format(
			sidecar_params.subtitle_sidecar_fmt(), sidecar_params));
		if (!subtitle_encoder_) {
			set_error(tr("Failed to create subtitle encoder"));
			return false;
		}

		if (!subtitle_encoder_->open()) {
			set_error(tr("Failed to open subtitle sidecar file: %1")
						 .arg(sidecar_filename));
			return false;
		}
	} else {
		subtitle_encoder_ = encoder_;
	}

	if (params_.has_custom_range()) {
		// Render custom range only
		export_range_ = params_.custom_range();
	} else {
		// Render entire sequence
		export_range_ = TimeRange(0, viewer()->get_length());
	}

	frame_time_ = 0;

	QSize video_force_size;
	QMatrix4x4 video_force_matrix;

	if (params_.video_enabled()) {
		// If a transformation matrix is applied to this video, create it here
		if (video_params().width() != params_.video_params().width() ||
			video_params().height() != params_.video_params().height()) {
			video_force_size = QSize(params_.video_params().width(),
									 params_.video_params().height());

			if (params_.video_scaling_method() != EncodingParams::k_stretch) {
				video_force_matrix = EncodingParams::generate_matrix(
					params_.video_scaling_method(), video_params().width(),
					video_params().height(), params_.video_params().width(),
					params_.video_params().height());
			}
		} else {
			// Disables forcing size in the renderer
			video_force_size = QSize(0, 0);
		}

		// Create color processor
		color_processor_ = ColorProcessor::create(
			color_manager_, color_manager_->get_reference_color_space(),
			params_.color_transform());
	}

	// Start render process
	TimeRangeList video_range, audio_range;
	TimeRange subtitle_range;

	if (params_.video_enabled()) {
		if (export_range_.in() > 0) {
			export_range_.set_in(Timecode::snap_time_to_timebase(
				export_range_.in(), video_params().frame_rate_as_time_base()));
		}

		video_range = { export_range_ };
	}

	if (params_.audio_enabled()) {
		audio_range = { export_range_ };
	}

	if (subtitles_enabled) {
		subtitle_range = export_range_;
	}

	render(color_manager_, video_range, audio_range, subtitle_range,
		   RenderMode::k_online, nullptr, video_force_size, video_force_matrix,
		   encoder_->get_desired_pixel_format(), VideoParams::k_rgba_channel_count,
		   color_processor_, params_.color_transform());

	bool success = true;

	encoder_->close();
	if (!encoder_->get_error().isEmpty()) {
		set_error(encoder_->get_error());
		success = false;
	}

	if (subtitle_encoder_ != encoder_) {
		subtitle_encoder_->close();
		if (!subtitle_encoder_->get_error().isEmpty()) {
			set_error(subtitle_encoder_->get_error());
			success = false;
		}
	}

	// If cancelled, delete the file we made, which is always a file we created since we write to a
	// temp file during the actual encoding process
	if (is_cancelled()) {
		QFile::remove(params_.filename());
	} else if (params_.filename() != real_filename) {
		// If we were writing to a temp file, overwrite now
		if (!FileFunctions::rename_file_allow_overwrite(params_.filename(),
													 real_filename)) {
			set_error(
				tr("Failed to overwrite \"%1\". Export has been saved as \"%2\" instead.")
					.arg(real_filename, params_.filename()));
			success = false;
		}
	}

	return success;
}

bool ExportTask::frame_downloaded(FramePtr f, const Rational &time)
{
	Rational actual_time = time - export_range_.in();

	time_map_.insert(actual_time, f);

	while (!is_cancelled()) {
		Rational real_time = Timecode::timestamp_to_time(
			frame_time_, video_params().frame_rate_as_time_base());

		if (!time_map_.contains(real_time)) {
			break;
		}

		// Unfortunately this can't be done in another thread since the frames need to be sent
		// one after the other chronologically.
		if (!encoder_->write_frame(time_map_.take(real_time), real_time)) {
			set_error(encoder_->get_error());
			return false;
		}

		frame_time_++;
		emit progress_changed(double(frame_time_) /
							 double(get_total_number_of_frames()));
	}

	return true;
}

bool ExportTask::audio_downloaded(const TimeRange &range,
								 const SampleBuffer &samples)
{
	TimeRange adjusted_range = range - export_range_.in();

	if (adjusted_range.in() == audio_time_) {
		if (!write_audio_loop(adjusted_range, samples)) {
			return false;
		}
	} else {
		audio_map_.insert(adjusted_range, samples);
	}

	return true;
}

bool ExportTask::encode_subtitle(const SubtitleBlock *sub)
{
	if (!subtitle_encoder_->write_subtitle(sub)) {
		set_error(subtitle_encoder_->get_error());
		return false;
	} else {
		return true;
	}
}

bool ExportTask::write_audio_loop(const TimeRange &time,
								const SampleBuffer &samples)
{
	if (!encoder_->write_audio(samples)) {
		set_error(encoder_->get_error());
		return false;
	}

	audio_time_ = time.out();

	for (auto it = audio_map_.begin(); it != audio_map_.end(); it++) {
		TimeRange t = it.key();
		SampleBuffer s = it.value();

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
