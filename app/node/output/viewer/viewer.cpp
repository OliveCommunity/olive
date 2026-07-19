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

#include "config/config.h"
#include "core.h"
#include "node/traverser.h"

namespace olive
{

const QString ViewerOutput::k_video_params_input =
	QStringLiteral("video_param_in");
const QString ViewerOutput::k_audio_params_input =
	QStringLiteral("audio_param_in");
const QString ViewerOutput::k_subtitle_params_input =
	QStringLiteral("subtitle_param_in");
const QString ViewerOutput::k_texture_input = QStringLiteral("tex_in");
const QString ViewerOutput::k_samples_input = QStringLiteral("samples_in");

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
		add_stream(Track::k_video, QVariant());
		add_stream(Track::k_audio, QVariant());
		set_default_parameters();
	}

	set_flag(k_dont_show_in_param_view);

	workarea_ = new TimelineWorkArea(this);
	markers_ = new TimelineMarkerList(this);
}

QString ViewerOutput::name() const
{
	return tr("Viewer");
}

QString ViewerOutput::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.vieweroutput");
}

QVector<Node::CategoryID> ViewerOutput::category() const
{
	return { k_category_output };
}

QString ViewerOutput::description() const
{
	return tr("Interface between a Viewer panel and the node system.");
}

QVariant ViewerOutput::data(const DataType &d) const
{
	switch (d) {
	case duration: {
		Rational using_timebase;
		Timecode::Display using_display =
			Core::instance()->get_timecode_display();

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
			return QString::fromStdString(Timecode::time_to_timecode(
				get_length(), using_timebase, using_display));
		}
		break;
	}
	case frequency_rate: {
		VideoParams video_stream;

		if (has_enabled_video_streams() &&
			(video_stream = get_first_enabled_video_stream()).video_type() !=
				VideoParams::k_video_type_still) {
			// This is a video editor, prioritize video streams
			return tr("%1 FPS").arg(video_stream.frame_rate().to_double());
		} else if (has_enabled_audio_streams()) {
			// No video streams, return audio
			AudioParams audio_stream = get_first_enabled_audio_stream();
			return tr("%1 Hz").arg(audio_stream.sample_rate());
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
	int width = OAK_CONFIG("DefaultSequenceWidth").toInt();
	int height = OAK_CONFIG("DefaultSequenceHeight").toInt();

	set_video_params(VideoParams(
		width, height,
		OAK_CONFIG("DefaultSequenceFrameRate").value<Rational>(),
		static_cast<PixelFormat::Format>(
			OAK_CONFIG("OfflinePixelFormat").toInt()),
		VideoParams::k_internal_channel_count,
		OAK_CONFIG("DefaultSequencePixelAspect").value<Rational>(),
		OAK_CONFIG("DefaultSequenceInterlacing")
			.value<VideoParams::Interlacing>(),
		1));
	set_audio_params(
		AudioParams(OAK_CONFIG("DefaultSequenceAudioFrequency").toInt(),
					OAK_CONFIG("DefaultSequenceAudioLayout").toULongLong(),
					k_default_sample_format));
}

void ViewerOutput::invalidate_cache(const TimeRange &range, const QString &from,
								   int element, InvalidateCacheOptions options)
{
	Q_UNUSED(element)

	if (Node *connected = get_connected_output(from, element)) {
		if (from == k_texture_input) {
			//connected->thumbnail_cache()->Request(range.Intersected(max_range), PlaybackCache::kPreviewsOnly);
			if (autocache_input_video_) {
				TimeRange max_range = input_time_adjustment(
					from, element, TimeRange(0, get_video_length()), false);
				connected->video_frame_cache()->request(
					this, range.intersected(max_range));
			}
		} else if (from == k_samples_input) {
			TimeRange max_range = input_time_adjustment(
				from, element, TimeRange(0, get_audio_length()), false);
			if (waveform_requests_enabled_) {
				connected->waveform_cache()->request(
					this, range.intersected(max_range));
			}
			if (autocache_input_audio_) {
				connected->audio_playback_cache()->request(
					this, range.intersected(max_range));
			}
		}
	}

	verify_length();

	super::invalidate_cache(range, from, element, options);
}

QVector<Track::Reference> ViewerOutput::get_enabled_streams_as_references() const
{
	QVector<Track::Reference> refs;

	{
		int vp_sz = get_video_stream_count();

		for (int i = 0; i < vp_sz; i++) {
			if (get_video_params(i).enabled()) {
				refs.append(Track::Reference(Track::k_video, i));
			}
		}
	}

	{
		int ap_sz = get_audio_stream_count();

		for (int i = 0; i < ap_sz; i++) {
			if (get_audio_params(i).enabled()) {
				refs.append(Track::Reference(Track::k_audio, i));
			}
		}
	}

	{
		int sp_sz = get_subtitle_stream_count();

		for (int i = 0; i < sp_sz; i++) {
			if (get_subtitle_params(i).enabled()) {
				refs.append(Track::Reference(Track::k_subtitle, i));
			}
		}
	}

	return refs;
}

void ViewerOutput::retranslate()
{
	super::retranslate();

	set_input_name(k_video_params_input, tr("Video Parameters"));
	set_input_name(k_audio_params_input, tr("Audio Parameters"));
	set_input_name(k_subtitle_params_input, tr("Subtitle Parameters"));

	if (has_input_with_id(k_texture_input)) {
		set_input_name(k_texture_input, tr("Texture"));
	}

	if (has_input_with_id(k_samples_input)) {
		set_input_name(k_samples_input, tr("Samples"));
	}
}

void ViewerOutput::verify_length()
{
	video_length_ = verify_length_internal(Track::k_video);

	audio_length_ = verify_length_internal(Track::k_audio);

	Rational subtitle_length = verify_length_internal(Track::k_subtitle);

	Rational real_length =
		qMax(subtitle_length, qMax(video_length_, audio_length_));

	if (real_length != last_length_) {
		last_length_ = real_length;
		emit length_changed(last_length_);
	}
}

void ViewerOutput::set_playhead(const Rational &t)
{
	playhead_ = t;
	emit playhead_changed(t);
}

void ViewerOutput::InputConnectedEvent(const QString &input, int element,
									   Node *output)
{
	if (input == k_texture_input) {
		emit texture_input_changed();
	} else if (input == k_samples_input) {
		connect(output->waveform_cache(), &AudioWaveformCache::validated, this,
				&ViewerOutput::connected_waveform_changed);
	}

	super::InputConnectedEvent(input, element, output);
}

void ViewerOutput::InputDisconnectedEvent(const QString &input, int element,
										  Node *output)
{
	if (input == k_texture_input) {
		emit texture_input_changed();
	} else if (input == k_samples_input) {
		disconnect(output->waveform_cache(), &AudioWaveformCache::validated,
				   this, &ViewerOutput::connected_waveform_changed);
	}

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
			Rational r = t.get(NodeValue::k_rational, QStringLiteral("length"))
							 .to_rational();
			if (!r.isNaN()) {
				return r;
			}
		}
		break;
	case Track::k_audio:
		if (is_input_connected(k_samples_input)) {
			NodeValueTable t = traverser.generate_table(
				get_connected_output(k_samples_input), TimeRange(0, 0));
			Rational r = t.get(NodeValue::k_rational, QStringLiteral("length"))
							 .to_rational();
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
			TimeRangeList invalid =
				connected->waveform_cache()->get_invalidated_ranges(max_range);
			for (const TimeRange &r : invalid) {
				connected->waveform_cache()->request(this, r);
			}
		}
	}
}

void ViewerOutput::value(const NodeValueRow &value, const NodeGlobals &globals,
						 NodeValueTable *table) const
{
	if (has_input_with_id(k_texture_input)) {
		NodeValue repush = value[k_texture_input];
		repush.set_tag(Track::Reference(Track::k_video, 0).to_string());
		table->push(repush);
	}
	if (has_input_with_id(k_samples_input)) {
		NodeValue repush = value[k_samples_input];
		repush.set_tag(Track::Reference(Track::k_audio, 0).to_string());
		table->push(repush);
	}
}

bool ViewerOutput::load_custom(QXmlStreamReader *reader, SerializedData *data)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("markers")) {
			if (!this->get_markers()->load(reader)) {
				return false;
			}
		} else if (reader->name() == QStringLiteral("workarea")) {
			if (!this->get_work_area()->load(reader)) {
				return false;
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	return true;
}

void ViewerOutput::save_custom(QXmlStreamWriter *writer) const
{
	writer->writeStartElement(QStringLiteral("workarea"));
	this->get_work_area()->save(writer);
	writer->writeEndElement(); // workarea

	writer->writeStartElement(QStringLiteral("markers"));
	this->get_markers()->save(writer);
	writer->writeEndElement(); // markers
}

void ViewerOutput::InputValueChangedEvent(const QString &input, int element)
{
	if (element == 0) {
		if (input == k_video_params_input) {
			VideoParams new_video_params = get_video_params();

			bool has_size_changed =
				cached_video_params_.width() != new_video_params.width() ||
				cached_video_params_.height() != new_video_params.height();
			bool has_frame_rate_changed = cached_video_params_.frame_rate() !=
									  new_video_params.frame_rate();
			bool has_pixel_aspect_changed =
				cached_video_params_.pixel_aspect_ratio() !=
				new_video_params.pixel_aspect_ratio();
			bool has_interlacing_changed = cached_video_params_.interlacing() !=
									   new_video_params.interlacing();

			if (has_size_changed) {
				emit size_changed(new_video_params.width(),
								 new_video_params.height());
			}

			if (has_pixel_aspect_changed) {
				emit pixel_aspect_changed(new_video_params.pixel_aspect_ratio());
			}

			if (has_interlacing_changed) {
				emit interlacing_changed(new_video_params.interlacing());
			}

			if (has_frame_rate_changed) {
				emit frame_rate_changed(new_video_params.frame_rate());
			}

			emit video_params_changed();

			cached_video_params_ = new_video_params;

		} else if (input == k_audio_params_input) {
			AudioParams new_audio_params = get_audio_params();

			bool has_sample_rate_changed = new_audio_params.sample_rate() !=
									   cached_audio_params_.sample_rate();

			if (has_sample_rate_changed) {
				emit sample_rate_changed(new_audio_params.sample_rate());
			}

			emit audio_params_changed();

			cached_audio_params_ = new_audio_params;
		}
	}

	super::InputValueChangedEvent(input, element);
}

void ViewerOutput::set_parameters_from_footage(
	const QVector<ViewerOutput *> footage)
{
	foreach (ViewerOutput *f, footage) {
		QVector<VideoParams> video_streams = f->get_enabled_video_streams();
		QVector<AudioParams> audio_streams = f->get_enabled_audio_streams();

		for (int i = 0; i < video_streams.size(); i++) {
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
								OAK_CONFIG("OfflinePixelFormat").toInt()),
							VideoParams::k_internal_channel_count,
							s.pixel_aspect_ratio(), s.interlacing(), 1));

			if (found_video_params) {
				break;
			}
		}

		if (!audio_streams.isEmpty()) {
			const AudioParams &s = audio_streams.first();
			set_audio_params(AudioParams(s.sample_rate(), s.channel_layout(),
									   k_default_sample_format));
		}
	}
}

int ViewerOutput::add_stream(Track::Type type, const QVariant &value)
{
	return set_stream(type, value, -1);
}

int ViewerOutput::set_stream(Track::Type type, const QVariant &value,
							int index_in)
{
	QString id;

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

QVector<VideoParams> ViewerOutput::get_enabled_video_streams() const
{
	QVector<VideoParams> streams;

	int vp_sz = get_video_stream_count();

	for (int i = 0; i < vp_sz; i++) {
		VideoParams vp = get_video_params(i);

		if (vp.enabled()) {
			streams.append(vp);
		}
	}

	return streams;
}

QVector<AudioParams> ViewerOutput::get_enabled_audio_streams() const
{
	QVector<AudioParams> streams;

	int ap_sz = get_audio_stream_count();

	for (int i = 0; i < ap_sz; i++) {
		AudioParams ap = get_audio_params(i);

		if (ap.enabled()) {
			streams.append(ap);
		}
	}

	return streams;
}

}
