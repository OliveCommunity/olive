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

#include "viewer.h"

#include "common/xmlutils.h"
#include "configaccessor.h"
#include "../../../c_api/nodehandle.h"

namespace
{

/**
 * @brief PlaybackCache::request(context, range) through the oakrender
 *        C ABI; the context handle is a borrowed box made on the spot.
 */
void request_cache_range(const OakRenderCache &cache, olive::Node *context,
						 const olive::TimeRange &range)
{
	OakNodeNode ctx = oaknode_c_api::make_handle<OakNodeNode>(
		context, false, nullptr);
	oakrender_cache_request(cache, ctx, range.in().numerator(),
							range.in().denominator(),
							range.out().numerator(),
							range.out().denominator());
	ctx.release(ctx.ctx);
}

} // namespace
#include "coreengine.h"
#include "traverser.h"
#include "olive/core/util/timecodefunctions.h"

namespace olive
{

using core::Timecode;

const std::string ViewerOutput::k_video_params_input = "video_param_in";
const std::string ViewerOutput::k_audio_params_input = "audio_param_in";
const std::string ViewerOutput::k_subtitle_params_input = "subtitle_param_in";
const std::string ViewerOutput::k_texture_input = "tex_in";
const std::string ViewerOutput::k_samples_input = "samples_in";

const SampleFormat ViewerOutput::k_default_sample_format = SampleFormat::f32_p;

#define super Node

ViewerOutput::ViewerOutput(bool create_buffer_inputs,
						   bool create_default_streams)
	: last_length_(0)
	, video_length_(0)
	, audio_length_(0)
	, autocache_input_video_(true)
	, autocache_input_audio_(false)
	, waveform_requests_enabled_(false)
{
	add_input(k_video_params_input, NodeValue::k_video_params,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable |
						k_input_flag_array | k_input_flag_hidden));

	add_input(k_audio_params_input, NodeValue::k_audio_params,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable |
						k_input_flag_array | k_input_flag_hidden));

	add_input(k_subtitle_params_input, NodeValue::k_subtitle_params,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable |
						k_input_flag_array | k_input_flag_hidden));

	if (create_buffer_inputs) {
		add_input(k_texture_input, NodeValue::k_texture,
				 InputFlags(k_input_flag_not_keyframable));
		add_input(k_samples_input, NodeValue::k_samples,
				 InputFlags(k_input_flag_not_keyframable));
	}

	if (create_default_streams) {
		add_stream(Track::k_video, Variant());
		add_stream(Track::k_audio, Variant());
		set_default_parameters();
	}

	set_flag(k_dont_show_in_param_view);

	workarea_ = oaktimeline_workarea_create();
	markers_ = oaktimeline_marker_list_create();
}

ViewerOutput::~ViewerOutput()
{
	disconnect_all();
	oaktimeline_workarea_free(&workarea_);
	oaktimeline_marker_list_free(&markers_);
}

std::string ViewerOutput::name() const
{
	return "Viewer";
}

std::string ViewerOutput::id() const
{
	return "org.olivevideoeditor.Olive.vieweroutput";
}

std::vector<Node::CategoryID> ViewerOutput::category() const
{
	return { k_category_output };
}

std::string ViewerOutput::description() const
{
	return "Interface between a Viewer panel and the node system.";
}

Variant ViewerOutput::data(const DataType &d) const
{
	switch (d) {
	case duration: {
		Rational using_timebase;
		Timecode::Display using_display =
			EngineCore::instance()->get_timecode_display();

		// Get first enabled streams
		VideoParams video = get_first_enabled_video_stream();
		AudioParams audio = get_first_enabled_audio_stream();
		SubtitleParams sub = get_first_enabled_subtitle_stream();

		if (video.is_valid() &&
			video.video_type() != VideoParams::k_video_type_still) {
			// Prioritize video
			using_timebase = video.frame_rate_as_time_base();
		} else if (audio.is_valid()) {
			// Use audio as a backup
			// If we're showing in a timecode, we prefer showing audio in seconds instead
			if (using_display == Timecode::k_timecode_drop_frame ||
				using_display == Timecode::k_timecode_non_drop_frame) {
				using_display = Timecode::k_timecode_seconds;
			}

			using_timebase = audio.sample_rate_as_time_base();
		} else if (sub.is_valid()) {
			using_timebase =
				OAK_CONFIG("DefaultSequenceFrameRate").value<Rational>();
		}

		if (!using_timebase.isNull()) {
			// Return time transformed to timecode
			return Timecode::time_to_timecode(get_length(), using_timebase,
											  using_display);
		}
		break;
	}
	case frequency_rate: {
		VideoParams video_stream;

		if (has_enabled_video_streams() &&
			(video_stream = get_first_enabled_video_stream()).video_type() !=
				VideoParams::k_video_type_still) {
			// This is a video editor, prioritize video streams
			return Variant(video_stream.frame_rate().to_double()).to_string() +
				   " FPS";
		} else if (has_enabled_audio_streams()) {
			// No video streams, return audio
			AudioParams audio_stream = get_first_enabled_audio_stream();
			return std::to_string(audio_stream.sample_rate()) + " Hz";
		}
		break;
	}
	default:
		break;
	}

	return super::data(d);
}

bool ViewerOutput::has_enabled_video_streams() const
{
	return get_first_enabled_video_stream().is_valid();
}

bool ViewerOutput::has_enabled_audio_streams() const
{
	return get_first_enabled_audio_stream().is_valid();
}

bool ViewerOutput::has_enabled_subtitle_streams() const
{
	return get_first_enabled_subtitle_stream().is_valid();
}

VideoParams ViewerOutput::get_first_enabled_video_stream() const
{
	int sz = get_video_stream_count();

	for (int i = 0; i < sz; i++) {
		VideoParams vp = get_video_params(i);

		if (vp.enabled()) {
			return vp;
		}
	}

	return VideoParams();
}

AudioParams ViewerOutput::get_first_enabled_audio_stream() const
{
	int sz = get_audio_stream_count();

	for (int i = 0; i < sz; i++) {
		AudioParams ap = get_audio_params(i);

		if (ap.enabled()) {
			return ap;
		}
	}

	return AudioParams();
}

SubtitleParams ViewerOutput::get_first_enabled_subtitle_stream() const
{
	int sz = get_subtitle_stream_count();

	for (int i = 0; i < sz; i++) {
		SubtitleParams sp = get_subtitle_params(i);

		if (sp.enabled()) {
			return sp;
		}
	}

	return SubtitleParams();
}

void ViewerOutput::set_default_parameters()
{
	int width = OAK_CONFIG("DefaultSequenceWidth").to_int();
	int height = OAK_CONFIG("DefaultSequenceHeight").to_int();

	set_video_params(VideoParams(
		width, height,
		OAK_CONFIG("DefaultSequenceFrameRate").value<Rational>(),
		static_cast<PixelFormat::Format>(
			OAK_CONFIG("OfflinePixelFormat").to_int()),
		VideoParams::k_internal_channel_count,
		OAK_CONFIG("DefaultSequencePixelAspect").value<Rational>(),
		OAK_CONFIG("DefaultSequenceInterlacing")
			.value<VideoParams::Interlacing>(),
		1));
	set_audio_params(
		AudioParams(OAK_CONFIG("DefaultSequenceAudioFrequency").to_int(),
					OAK_CONFIG("DefaultSequenceAudioLayout").to_u_long_long(),
					k_default_sample_format));
}

void ViewerOutput::invalidate_cache(const TimeRange &range,
									const std::string &from, int element,
									InvalidateCacheOptions options)
{
	(void) element;

	if (Node *connected = get_connected_output(from, element)) {
		if (from == k_texture_input) {
			//connected->thumbnail_cache()->Request(range.Intersected(max_range), PlaybackCache::kPreviewsOnly);
			if (autocache_input_video_) {
				TimeRange max_range = input_time_adjustment(
					from, element, TimeRange(0, get_video_length()), false);
				request_cache_range(connected->video_frame_cache(), this,
									range.intersected(max_range));
			}
		} else if (from == k_samples_input) {
			TimeRange max_range = input_time_adjustment(
				from, element, TimeRange(0, get_audio_length()), false);
			if (waveform_requests_enabled_) {
				request_cache_range(connected->waveform_cache(), this,
									range.intersected(max_range));
			}
			if (autocache_input_audio_) {
				request_cache_range(connected->audio_playback_cache(), this,
									range.intersected(max_range));
			}
		}
	}

	verify_length();

	super::invalidate_cache(range, from, element, options);
}

std::vector<Track::Reference> ViewerOutput::get_enabled_streams_as_references() const
{
	std::vector<Track::Reference> refs;

	{
		int vp_sz = get_video_stream_count();

		for (int i = 0; i < vp_sz; i++) {
			if (get_video_params(i).enabled()) {
				refs.push_back(Track::Reference(Track::k_video, i));
			}
		}
	}

	{
		int ap_sz = get_audio_stream_count();

		for (int i = 0; i < ap_sz; i++) {
			if (get_audio_params(i).enabled()) {
				refs.push_back(Track::Reference(Track::k_audio, i));
			}
		}
	}

	{
		int sp_sz = get_subtitle_stream_count();

		for (int i = 0; i < sp_sz; i++) {
			if (get_subtitle_params(i).enabled()) {
				refs.push_back(Track::Reference(Track::k_subtitle, i));
			}
		}
	}

	return refs;
}

void ViewerOutput::retranslate()
{
	super::retranslate();

	set_input_name(k_video_params_input, "Video Parameters");
	set_input_name(k_audio_params_input, "Audio Parameters");
	set_input_name(k_subtitle_params_input, "Subtitle Parameters");

	if (has_input_with_id(k_texture_input)) {
		set_input_name(k_texture_input, "Texture");
	}

	if (has_input_with_id(k_samples_input)) {
		set_input_name(k_samples_input, "Samples");
	}
}

void ViewerOutput::verify_length()
{
	video_length_ = verify_length_internal(Track::k_video);

	audio_length_ = verify_length_internal(Track::k_audio);

	Rational subtitle_length = verify_length_internal(Track::k_subtitle);

	Rational real_length =
		std::max(subtitle_length, std::max(video_length_, audio_length_));

	if (real_length != last_length_) {
		last_length_ = real_length;
	}
}

void ViewerOutput::set_playhead(const Rational &t)
{
	playhead_ = t;
}

void ViewerOutput::InputConnectedEvent(const std::string &input, int element,
									   Node *output)
{
	super::InputConnectedEvent(input, element, output);
}

void ViewerOutput::InputDisconnectedEvent(const std::string &input, int element,
										  Node *output)
{
	super::InputDisconnectedEvent(input, element, output);
}

Rational ViewerOutput::verify_length_internal(Track::Type type) const
{
	NodeTraverser traverser;

	switch (type) {
	case Track::k_video:
		if (is_input_connected(k_texture_input)) {
			NodeValueTable t = traverser.generate_table(
				get_connected_output(k_texture_input), TimeRange(0, 0));
			Rational r =
				t.get(NodeValue::k_rational, "length").to_rational();
			if (!r.isNaN()) {
				return r;
			}
		}
		break;
	case Track::k_audio:
		if (is_input_connected(k_samples_input)) {
			NodeValueTable t = traverser.generate_table(
				get_connected_output(k_samples_input), TimeRange(0, 0));
			Rational r =
				t.get(NodeValue::k_rational, "length").to_rational();
			if (!r.isNaN()) {
				return r;
			}
		}
		break;
	case Track::k_none:
	case Track::k_subtitle:
	case Track::k_count:
		break;
	}

	return 0;
}

Node *ViewerOutput::get_connected_texture_output()
{
	return get_connected_output(k_texture_input);
}

Node::ValueHint ViewerOutput::get_connected_texture_value_hint()
{
	return get_value_hint_for_input(k_texture_input);
}

Node *ViewerOutput::get_connected_sample_output()
{
	return get_connected_output(k_samples_input);
}

Node::ValueHint ViewerOutput::get_connected_sample_value_hint()
{
	return get_value_hint_for_input(k_samples_input);
}

void ViewerOutput::set_waveform_enabled(bool e)
{
	if ((waveform_requests_enabled_ = e)) {
		if (Node *connected = this->get_connected_sample_output()) {
			TimeRange max_range = input_time_adjustment(
				k_samples_input, -1, TimeRange(0, get_audio_length()), false);
			const OakRenderCache &wave_cache =
				connected->waveform_cache();
			int n = oakrender_cache_get_invalidated_ranges(
				wave_cache, max_range.in().numerator(),
				max_range.in().denominator(), max_range.out().numerator(),
				max_range.out().denominator(), NULL, 0);
			if (n > 0) {
				std::vector<int64_t> ranges(size_t(n) * 4);
				oakrender_cache_get_invalidated_ranges(
					wave_cache, max_range.in().numerator(),
					max_range.in().denominator(),
					max_range.out().numerator(),
					max_range.out().denominator(), ranges.data(), n);
				for (int i = 0; i < n; i++) {
					request_cache_range(
						wave_cache, this,
						TimeRange(Rational(ranges[i * 4], ranges[i * 4 + 1]),
								  Rational(ranges[i * 4 + 2],
										   ranges[i * 4 + 3])));
				}
			}
		}
	}
}

void ViewerOutput::value(const NodeValueRow &value, const NodeGlobals &globals,
						 NodeValueTable *table) const
{
	if (has_input_with_id(k_texture_input)) {
		NodeValue repush = value.at(k_texture_input);
		repush.set_tag(Track::Reference(Track::k_video, 0).to_string());
		table->push(repush);
	}
	if (has_input_with_id(k_samples_input)) {
		NodeValue repush = value.at(k_samples_input);
		repush.set_tag(Track::Reference(Track::k_audio, 0).to_string());
		table->push(repush);
	}
}

bool ViewerOutput::load_custom(XmlStreamReader *reader, SerializedData *data)
{
	OakXmlReader xml = oakcommon_xml_reader_wrap_native(reader);
	if (!xml.ctx) {
		return false;
	}

	bool ok = true;
	while (ok && xml_read_next_start_element(reader)) {
		if (reader->name() == "markers") {
			ok = oaktimeline_marker_list_load(markers_, xml) ==
				OAKTIMELINE_OK;
		} else if (reader->name() == "workarea") {
			ok = oaktimeline_workarea_load(workarea_, xml) ==
				OAKTIMELINE_OK;
		} else {
			reader->skip_current_element();
		}
	}

	oakcommon_xml_reader_free(&xml);
	return ok;
}

void ViewerOutput::save_custom(XmlStreamWriter *writer) const
{
	OakXmlWriter xml = oakcommon_xml_writer_wrap_native(writer);
	if (!xml.ctx) {
		return;
	}

	writer->write_start_element("workarea");
	oaktimeline_workarea_save(workarea_, xml);
	writer->write_end_element(); // workarea

	writer->write_start_element("markers");
	oaktimeline_marker_list_save(markers_, xml);
	writer->write_end_element(); // markers

	oakcommon_xml_writer_free(&xml);
}

void ViewerOutput::InputValueChangedEvent(const std::string &input, int element)
{
	if (element == 0) {
		if (input == k_video_params_input) {
			VideoParams new_video_params = get_video_params();

			cached_video_params_ = new_video_params;

		} else if (input == k_audio_params_input) {
			AudioParams new_audio_params = get_audio_params();

			cached_audio_params_ = new_audio_params;
		}
	}

	super::InputValueChangedEvent(input, element);
}

void ViewerOutput::set_parameters_from_footage(
	const std::vector<ViewerOutput *> footage)
{
	for (ViewerOutput *f : footage) {
		std::vector<VideoParams> video_streams = f->get_enabled_video_streams();
		std::vector<AudioParams> audio_streams = f->get_enabled_audio_streams();

		for (int i = 0; i < int(video_streams.size()); i++) {
			const VideoParams &s = video_streams.at(i);

			bool found_video_params = false;
			Rational using_timebase;

			if (s.video_type() == VideoParams::k_video_type_still) {
				// If this is a still image, we'll use it's resolution but won't set
				// `found_video_params` in case something with a frame rate comes along which we'll
				// prioritize
				if (i > 0) {
					// Ignore still images past stream 0
					continue;
				}

				using_timebase = get_video_params().time_base();
			} else {
				using_timebase = s.frame_rate_as_time_base();
				found_video_params = true;
			}

			set_video_params(
				VideoParams(s.width(), s.height(), using_timebase,
							static_cast<PixelFormat::Format>(
								OAK_CONFIG("OfflinePixelFormat").to_int()),
							VideoParams::k_internal_channel_count,
							s.pixel_aspect_ratio(), s.interlacing(), 1));

			if (found_video_params) {
				break;
			}
		}

		if (!audio_streams.empty()) {
			const AudioParams &s = audio_streams.front();
			set_audio_params(AudioParams(s.sample_rate(), s.channel_layout(),
									   k_default_sample_format));
		}
	}
}

int ViewerOutput::add_stream(Track::Type type, const Variant &value)
{
	return set_stream(type, value, -1);
}

int ViewerOutput::set_stream(Track::Type type, const Variant &value,
							 int index_in)
{
	std::string id;

	if (type == Track::k_video) {
		id = k_video_params_input;
	} else if (type == Track::k_audio) {
		id = k_audio_params_input;
	} else if (type == Track::k_subtitle) {
		id = k_subtitle_params_input;
	} else {
		return -1;
	}

	// Add another video/audio param to the array for this stream
	int index = (index_in == -1) ? input_array_size(id) : index_in;

	if (index >= input_array_size(id)) {
		input_array_resize(id, index + 1);
	}

	set_standard_value(id, value, index);

	return index;
}

std::vector<VideoParams> ViewerOutput::get_enabled_video_streams() const
{
	std::vector<VideoParams> streams;

	int vp_sz = get_video_stream_count();

	for (int i = 0; i < vp_sz; i++) {
		VideoParams vp = get_video_params(i);

		if (vp.enabled()) {
			streams.push_back(vp);
		}
	}

	return streams;
}

std::vector<AudioParams> ViewerOutput::get_enabled_audio_streams() const
{
	std::vector<AudioParams> streams;

	int ap_sz = get_audio_stream_count();

	for (int i = 0; i < ap_sz; i++) {
		AudioParams ap = get_audio_params(i);

		if (ap.enabled()) {
			streams.push_back(ap);
		}
	}

	return streams;
}

}
