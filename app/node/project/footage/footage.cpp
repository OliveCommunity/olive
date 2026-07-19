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

#include "footage.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>

#include "codec/decoder.h"
#include "common/filefunctions.h"
#include "common/qtutils.h"
#include "common/xmlutils.h"
#include "config/config.h"
#include "core.h"
#include "render/job/footagejob.h"
#include "ui/icons/icons.h"

namespace olive
{

const QString Footage::k_filename_input = QStringLiteral("file_in");

#define super ViewerOutput

Footage::Footage(const QString &filename)
	: ViewerOutput(false, false)
	, timestamp_(0)
	, has_source_start_time_(false)
	, proxy_enabled_(false)
	, proxy_state_(ProxyManager::k_proxy_missing)
	, proxy_video_stream_index_(-1)
	, proxy_preset_version_(0)
	, has_custom_proxy_params_(false)
	, valid_(false)
	, cancelled_(nullptr)
	, total_stream_count_(0)
{
	set_flag(k_is_item);

	prepend_input(k_filename_input, NodeValue::k_file,
				 InputFlags(k_input_flag_not_connectable |
							k_input_flag_not_keyframable));

	clear();

	if (!filename.isEmpty()) {
		set_filename(filename);
	}

	QTimer *check_timer = new QTimer(this);
	check_timer->setInterval(5000);
	connect(check_timer, &QTimer::timeout, this, &Footage::check_footage);
	check_timer->start();

	connect(this->waveform_cache(), &AudioWaveformCache::validated, this,
			&ViewerOutput::connected_waveform_changed);
}

void Footage::retranslate()
{
	super::retranslate();

	set_input_name(k_filename_input, tr("Filename"));
}

void Footage::InputValueChangedEvent(const QString &input, int element)
{
	if (input == k_filename_input) {
		// Reset internal stream cache
		clear();

		reprobe();
	} else {
		super::InputValueChangedEvent(input, element);
	}
}

Rational Footage::verify_length_internal(Track::Type type) const
{
	if (type == Track::k_video) {
		VideoParams first_stream = get_first_enabled_video_stream();

		if (first_stream.is_valid()) {
			return Timecode::timestamp_to_time(first_stream.duration(),
											   first_stream.time_base());
		}
	} else if (type == Track::k_audio) {
		AudioParams first_stream = get_first_enabled_audio_stream();

		if (first_stream.is_valid()) {
			return Timecode::timestamp_to_time(first_stream.duration(),
											   first_stream.time_base());
		}
	} else if (type == Track::k_subtitle) {
		SubtitleParams first_stream = get_first_enabled_subtitle_stream();

		if (first_stream.is_valid()) {
			return first_stream.duration();
		}
	}

	return 0;
}

QString Footage::get_colorspace_to_use(const VideoParams &params) const
{
	if (params.colorspace().isEmpty()) {
		return project()->color_manager()->get_default_input_color_space();
	} else {
		return params.colorspace();
	}
}

void Footage::clear()
{
	// Clear all dynamically created inputs
	input_array_resize(k_video_params_input, 0);
	input_array_resize(k_audio_params_input, 0);
	input_array_resize(k_subtitle_params_input, 0);

	// Clear decoder link
	decoder_.clear();

	has_source_start_time_ = false;
	source_start_time_ = Rational();
	source_start_time_source_.clear();
	clear_proxy();

	// Clear total stream count
	total_stream_count_ = 0;

	// Reset ready state
	valid_ = false;
}

void Footage::set_valid()
{
	valid_ = true;
}

QString Footage::filename() const
{
	return get_standard_value(k_filename_input).toString();
}

void Footage::set_filename(const QString &s)
{
	set_standard_value(k_filename_input, s);
}

const qint64 &Footage::timestamp() const
{
	return timestamp_;
}

void Footage::set_timestamp(const qint64 &t)
{
	timestamp_ = t;
}

int Footage::get_stream_index(Track::Type type, int index) const
{
	switch (type) {
	case Track::k_video:
		if (index >= 0 && index < get_video_stream_count()) {
			return get_video_params(index).stream_index();
		}
		break;
	case Track::k_audio:
		if (index >= 0 && index < get_audio_stream_count()) {
			return get_audio_params(index).stream_index();
		}
		break;
	case Track::k_subtitle:
		if (index >= 0 && index < get_subtitle_stream_count()) {
			return get_subtitle_params(index).stream_index();
		}
		break;
	case Track::k_none:
	case Track::k_count:
		break;
	}

	return -1;
}

Track::Reference Footage::get_reference_from_real_index(int real_index) const
{
	// Check video streams
	for (int i = 0; i < get_video_stream_count(); i++) {
		if (get_video_params(i).stream_index() == real_index) {
			return Track::Reference(Track::k_video, i);
		}
	}

	for (int i = 0; i < get_audio_stream_count(); i++) {
		if (get_audio_params(i).stream_index() == real_index) {
			return Track::Reference(Track::k_audio, i);
		}
	}

	for (int i = 0; i < get_subtitle_stream_count(); i++) {
		if (get_subtitle_params(i).stream_index() == real_index) {
			return Track::Reference(Track::k_subtitle, i);
		}
	}

	return Track::Reference();
}

const QString &Footage::decoder() const
{
	return decoder_;
}

void Footage::set_source_start_time(const Rational &time, const QString &source)
{
	source_start_time_ = time;
	source_start_time_source_ = source;
	has_source_start_time_ = true;
}

void Footage::clear_source_start_time()
{
	source_start_time_ = Rational();
	source_start_time_source_.clear();
	has_source_start_time_ = false;
}

void Footage::set_proxy_enabled(bool enabled)
{
	if (proxy_enabled_ != enabled) {
		qDebug() << "Footage::set_proxy_enabled:" << filename() << enabled;
		proxy_enabled_ = enabled;
		if (Project *p = project()) {
			p->set_modified(true);
		}
		emit proxy_settings_changed();
	}
}

void Footage::set_proxy(const QString &path, ProxyManager::ProxyState state,
					   int video_stream_index, int preset_version, bool enabled)
{
	qDebug() << "Footage::SetProxy:" << filename() << "enabled=" << enabled
			 << "state=" << ProxyManager::proxy_state_to_string(state)
			 << "path=" << path;
	proxy_path_ = path;
	proxy_state_ = state;
	proxy_video_stream_index_ = video_stream_index;
	proxy_preset_version_ = preset_version;
	proxy_enabled_ = enabled;
	if (Project *p = project()) {
		p->set_modified(true);
	}
	emit proxy_settings_changed();
}

void Footage::clear_proxy()
{
	proxy_enabled_ = false;
	proxy_path_.clear();
	proxy_state_ = ProxyManager::k_proxy_missing;
	proxy_video_stream_index_ = -1;
	proxy_preset_version_ = 0;
	emit proxy_settings_changed();
}

void Footage::set_custom_proxy_params(const ProxyManager::ProxyParams &params)
{
	custom_proxy_params_ = params;
	has_custom_proxy_params_ = true;
	if (Project *p = project()) {
		p->set_modified(true);
	}
	emit proxy_settings_changed();
}

void Footage::clear_custom_proxy_params()
{
	if (has_custom_proxy_params_) {
		has_custom_proxy_params_ = false;
		custom_proxy_params_ = ProxyManager::ProxyParams();
		if (Project *p = project()) {
			p->set_modified(true);
		}
		emit proxy_settings_changed();
	}
}

ProxyManager::ProxyParams Footage::get_effective_proxy_params() const
{
	if (has_custom_proxy_params_) {
		return custom_proxy_params_;
	}

	return ProxyManager::proxy_params_from_config();
}

QString Footage::describe_video_stream(const VideoParams &params)
{
	if (params.video_type() == VideoParams::k_video_type_still) {
		return tr("%1: Image - %2x%3")
			.arg(QString::number(params.stream_index()),
				 QString::number(params.width()),
				 QString::number(params.height()));
	} else {
		return tr("%1: Video - %2x%3")
			.arg(QString::number(params.stream_index()),
				 QString::number(params.width()),
				 QString::number(params.height()));
	}
}

QString Footage::describe_audio_stream(const AudioParams &params)
{
	return tr("%1: Audio - %n Channel(s), %2Hz", nullptr,
			  params.channel_count())
		.arg(QString::number(params.stream_index()),
			 QString::number(params.sample_rate()));
}

QString Footage::describe_subtitle_stream(const SubtitleParams &params)
{
	return tr("%1: Subtitle").arg(QString::number(params.stream_index()));
}

void Footage::value(const NodeValueRow &value, const NodeGlobals &globals,
					NodeValueTable *table) const
{
	Q_UNUSED(globals)

	// Pop filename from table
	QString file = value[k_filename_input].to_string();

	// Proxies can be globally disabled (Tools > Use Proxy Media) without
	// losing each footage's individual proxy setting
	const bool proxies_allowed =
		Config::current()[QStringLiteral("UseProxyMedia")].toBool();

	// If the file exists and the reference is valid, push a footage job to the renderer
	if (QFileInfo::exists(file)) {
		// Push length
		table->push(NodeValue::k_rational, QVariant::fromValue(get_length()),
					this, QStringLiteral("length"));

		// Push each stream as a footage job
		for (int i = 0; i < get_total_stream_count(); i++) {
			Track::Reference ref = get_reference_from_real_index(i);
			FootageJob job(globals.time(), decoder_, filename(), ref.type(),
						   get_length(), globals.loop_mode());

			if (ref.type() == Track::k_video) {
				VideoParams vp = get_video_params(ref.index());

				if (proxies_allowed && proxy_enabled_ &&
					!proxy_path_.isEmpty() &&
					proxy_video_stream_index_ == vp.stream_index() &&
					ProxyManager::get_proxy_state(proxy_path_) ==
						ProxyManager::k_proxy_ready) {
					job.set_proxy(proxy_path_, QStringLiteral("ffmpeg"), 0);
				}

				// Ensure the colorspace is valid and not empty
				vp.set_colorspace(get_colorspace_to_use(vp));

				// Adjust footage job's divider
				if (globals.vparams().divider() > 1) {
					// Use a divider appropriate for this target resolution
					int calculated = VideoParams::get_divider_for_target_resolution(
						vp.width(), vp.height(),
						globals.vparams().effective_width(),
						globals.vparams().effective_height());
					vp.set_divider(
						std::min(calculated, globals.vparams().divider()));
				} else {
					// Render everything at full res
					vp.set_divider(1);
				}

				job.set_video_params(vp);

				table->push(NodeValue::k_texture, Texture::job(vp, job), this,
							ref.to_string());
			} else if (ref.type() == Track::k_audio) {
				AudioParams ap = get_audio_params(ref.index());
				job.set_audio_params(ap);
				job.set_cache_path(project()->cache_path());

				// Proxies generated with audio contain the video stream at
				// index 0 followed by all source audio streams in source order
				if (proxies_allowed && proxy_enabled_ &&
					!proxy_path_.isEmpty() &&
					ProxyManager::get_proxy_state(proxy_path_) ==
						ProxyManager::k_proxy_ready &&
					ProxyManager::proxy_filename_has_audio(proxy_path_)) {
					int audio_rank = 0;
					for (int i = 0; i < get_total_stream_count(); i++) {
						const Track::Reference other =
							get_reference_from_real_index(i);
						if (other.type() == Track::k_audio &&
							get_audio_params(other.index()).stream_index() <
								ap.stream_index()) {
							audio_rank++;
						}
					}
					job.set_proxy(proxy_path_, QStringLiteral("ffmpeg"),
								  audio_rank + 1);
				}

				table->push(NodeValue::k_samples, QVariant::fromValue(job), this,
							ref.to_string());
			}
		}
	} else if (!file.isEmpty()) {
		// Media is offline: push a generated warning frame for each video
		// stream so missing media is clearly visible in the timeline instead
		// of a transparent/black hole. generate_frame() draws the slat.
		for (int i = 0; i < get_total_stream_count(); i++) {
			Track::Reference ref = get_reference_from_real_index(i);
			if (ref.type() != Track::k_video) {
				continue;
			}

			VideoParams vp = get_video_params(ref.index());
			if (!vp.is_valid()) {
				vp = globals.vparams();
			}
			vp.set_format(PixelFormat::u8);
			vp.set_colorspace(
				project()->color_manager()->get_default_input_color_space());

			GenerateJob job(value);
			table->push(NodeValue::k_texture, Texture::job(vp, job), this,
						ref.to_string());
		}
	}
}

void Footage::generate_frame(FramePtr frame, const GenerateJob &job) const
{
	Q_UNUSED(job)

	QImage img(reinterpret_cast<uchar *>(frame->data()), frame->width(),
			   frame->height(), frame->linesize_bytes(),
			   QImage::Format_RGBA8888_Premultiplied);

	// Dark red slat with diagonal stripes, matching the offline media
	// warnings of other NLEs
	img.fill(QColor(60, 0, 0));

	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);

	p.setPen(QPen(QColor(120, 20, 20), qMax(2, frame->height() / 90)));
	const int stripe_step = qMax(16, frame->height() / 6);
	for (int x = -frame->height(); x < frame->width() + frame->height();
		 x += stripe_step) {
		p.drawLine(x, 0, x + frame->height(), frame->height());
	}

	QFont font = p.font();
	font.setPixelSize(qMax(10, frame->height() / 10));
	font.setBold(true);
	p.setFont(font);
	p.setPen(Qt::white);
	p.drawText(img.rect(), Qt::AlignCenter | Qt::TextWordWrap,
			   tr("Media Offline\n%1").arg(QFileInfo(filename()).fileName()));
}

QString Footage::get_stream_type_name(Track::Type type)
{
	switch (type) {
	case Track::k_video:
		return tr("Video");
	case Track::k_audio:
		return tr("Audio");
	case Track::k_subtitle:
		return tr("Subtitle");
	case Track::k_none:
	case Track::k_count:
		break;
	}

	return tr("Unknown");
}

Node *Footage::get_connected_texture_output()
{
	if (get_video_stream_count() > 0) {
		return this;
	} else {
		return nullptr;
	}
}

Node *Footage::get_connected_sample_output()
{
	if (get_audio_stream_count() > 0) {
		return this;
	} else {
		return nullptr;
	}
}

bool time_is_out_of_bounds(const Rational &time, const Rational &length)
{
	return time < 0 || time >= length;
}

Rational Footage::adjust_time_by_loop_mode(Rational time, LoopMode loop_mode,
									   const Rational &length,
									   VideoParams::Type type,
									   const Rational &timebase)
{
	if (type == VideoParams::k_video_type_still) {
		// No looping for still images
		return 0;
	}

	if (time_is_out_of_bounds(time, length)) {
		switch (loop_mode) {
		case LoopMode::k_loop_mode_off:
			// Return no time to indicate no frame should be shown here
			time = Rational::na_n;
			break;
		case LoopMode::k_loop_mode_clamp:
			if (length < timebase) {
				// No full frame fits in the range, so there is nothing to clamp to
				time = Rational::na_n;
			} else {
				// Clamp footage time to length
				time = std::clamp(time, Rational(0), length - timebase);
			}
			break;
		case LoopMode::k_loop_mode_loop:
			if (length <= 0) {
				// Cannot loop around an empty range
				time = Rational::na_n;
			} else {
				// Loop footage time around job length
				do {
					if (time >= length) {
						time -= length;
					} else {
						time += length;
					}
				} while (time_is_out_of_bounds(time, length));
			}
			break;
		}
	}

	return time;
}

QVariant Footage::data(const DataType &d) const
{
	switch (d) {
	case created_time: {
		QFileInfo info(filename());

		if (info.exists()) {
			return QtUtils::get_creation_date(info).toSecsSinceEpoch();
		}
		break;
	}
	case modified_time: {
		QFileInfo info(filename());

		if (info.exists()) {
			return info.lastModified().toSecsSinceEpoch();
		}
		break;
	}
	case icon: {
		if (valid_ && get_total_stream_count()) {
			// Prioritize video > audio > image
			VideoParams s = get_first_enabled_video_stream();

			if (s.is_valid() &&
				s.video_type() != VideoParams::k_video_type_still) {
				return icon::video;
			} else if (has_enabled_audio_streams()) {
				return icon::audio;
			} else if (s.is_valid() &&
					   s.video_type() == VideoParams::k_video_type_still) {
				return icon::image;
			} else if (has_enabled_subtitle_streams()) {
				return icon::subtitles;
			}
		}

		return icon::error;
	}
	case tooltip: {
		if (valid_) {
			QString tip = tr("Filename: %1").arg(filename());

			int vp_sz = get_video_stream_count();
			for (int i = 0; i < vp_sz; i++) {
				VideoParams p = get_video_params(i);

				if (p.enabled()) {
					tip.append("\n");
					tip.append(describe_video_stream(p));
				}
			}

			int ap_sz = get_audio_stream_count();
			for (int i = 0; i < ap_sz; i++) {
				AudioParams p = get_audio_params(i);

				if (p.enabled()) {
					tip.append("\n");
					tip.append(describe_audio_stream(p));
				}
			}

			int sp_sz = get_subtitle_stream_count();
			for (int i = 0; i < sp_sz; i++) {
				SubtitleParams p = get_subtitle_params(i);

				if (p.enabled()) {
					tip.append("\n");
					tip.append(describe_subtitle_stream(p));
				}
			}

			return tip;
		} else {
			return tr("Invalid");
		}
	}
	default:
		break;
	}

	return super::data(d);
}

bool Footage::load_custom(QXmlStreamReader *reader, SerializedData *data)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("timestamp")) {
			this->set_timestamp(reader->readElementText().toLongLong());
		} else if (reader->name() == QStringLiteral("proxy")) {
			bool enabled = false;
			ProxyManager::ProxyState state = ProxyManager::k_proxy_missing;
			int stream = -1;
			int preset_version = 0;
			bool has_custom_params = false;
			ProxyManager::ProxyParams custom_params;
			{
				XMLAttributeLoop(reader, attr)
				{
					if (attr.name() == QStringLiteral("enabled")) {
						enabled = (attr.value() == QStringLiteral("1") ||
								   attr.value() == QStringLiteral("true"));
					} else if (attr.name() == QStringLiteral("state")) {
						state = ProxyManager::proxy_state_from_string(
							attr.value().toString());
					} else if (attr.name() == QStringLiteral("stream")) {
						stream = attr.value().toInt();
					} else if (attr.name() == QStringLiteral("preset")) {
						preset_version = attr.value().toInt();
					} else if (attr.name() == QStringLiteral("custom")) {
						has_custom_params =
							(attr.value() == QStringLiteral("1") ||
							 attr.value() == QStringLiteral("true"));
					} else if (attr.name() == QStringLiteral("pwidth")) {
						custom_params.width = attr.value().toInt();
					} else if (attr.name() == QStringLiteral("pheight")) {
						custom_params.height = attr.value().toInt();
					} else if (attr.name() == QStringLiteral("pcrf")) {
						custom_params.crf = attr.value().toInt();
					} else if (attr.name() == QStringLiteral("ppreset")) {
						custom_params.preset = attr.value().toString();
					} else if (attr.name() == QStringLiteral("pext")) {
						custom_params.extension = attr.value().toString();
					} else if (attr.name() == QStringLiteral("paudio")) {
						custom_params.include_audio =
							(attr.value() == QStringLiteral("1") ||
							 attr.value() == QStringLiteral("true"));
					}
				}
			}

			if (has_custom_params) {
				set_custom_proxy_params(custom_params);
			}

			const QString path = reader->readElementText();
			if (!path.isEmpty()) {
				set_proxy(path, state, stream, preset_version, enabled);
			} else if (enabled) {
				set_proxy_enabled(true);
			}
		} else if (reader->name() == QStringLiteral("sourcestarttime")) {
			QString source;
			{
				XMLAttributeLoop(reader, attr)
				{
					if (attr.name() == QStringLiteral("source")) {
						source = attr.value().toString();
					}
				}
			}

			const QStringList split = reader->readElementText().split('/');
			if (split.size() == 2) {
				bool numerator_ok = false;
				bool denominator_ok = false;
				const int numerator = split.at(0).toInt(&numerator_ok);
				const int denominator = split.at(1).toInt(&denominator_ok);
				if (numerator_ok && denominator_ok && denominator) {
					set_source_start_time(Rational(numerator, denominator),
									   source);
				}
			}
		} else if (reader->name() == QStringLiteral("viewer")) {
			if (!ViewerOutput::load_custom(reader, data)) {
				return false;
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	// The cached lengths are not serialized. Recompute them from the stream
	// parameters that were just loaded so that worker processes and any code
	// that reads GetLength() before InvalidateCache() runs sees valid values.
	verify_length();

	return true;
}

void Footage::save_custom(QXmlStreamWriter *writer) const
{
	writer->writeTextElement(QStringLiteral("timestamp"),
							 QString::number(this->timestamp()));

	if (!proxy_path_.isEmpty() || proxy_enabled_) {
		writer->writeStartElement(QStringLiteral("proxy"));
		writer->writeAttribute(QStringLiteral("enabled"),
							   proxy_enabled_ ? QStringLiteral("1") :
												QStringLiteral("0"));
		writer->writeAttribute(QStringLiteral("state"),
							   ProxyManager::proxy_state_to_string(proxy_state_));
		writer->writeAttribute(QStringLiteral("stream"),
							   QString::number(proxy_video_stream_index_));
		writer->writeAttribute(QStringLiteral("preset"),
							   QString::number(proxy_preset_version_));
		if (has_custom_proxy_params_) {
			writer->writeAttribute(QStringLiteral("custom"),
								   QStringLiteral("1"));
			writer->writeAttribute(
				QStringLiteral("pwidth"),
				QString::number(custom_proxy_params_.width));
			writer->writeAttribute(
				QStringLiteral("pheight"),
				QString::number(custom_proxy_params_.height));
			writer->writeAttribute(
				QStringLiteral("pcrf"),
				QString::number(custom_proxy_params_.crf));
			writer->writeAttribute(QStringLiteral("ppreset"),
								   custom_proxy_params_.preset);
			writer->writeAttribute(QStringLiteral("pext"),
								   custom_proxy_params_.extension);
			writer->writeAttribute(
				QStringLiteral("paudio"),
				custom_proxy_params_.include_audio ? QStringLiteral("1") :
													 QStringLiteral("0"));
		}
		writer->writeCharacters(proxy_path_);
		writer->writeEndElement();
	}

	if (has_source_start_time_) {
		writer->writeStartElement(QStringLiteral("sourcestarttime"));
		writer->writeAttribute(QStringLiteral("source"),
							   source_start_time_source_);
		writer->writeCharacters(QStringLiteral("%1/%2").arg(
			QString::number(source_start_time_.numerator()),
			QString::number(source_start_time_.denominator())));
		writer->writeEndElement();
	}

	writer->writeStartElement(QStringLiteral("viewer"));

	ViewerOutput::save_custom(writer);

	writer->writeEndElement(); // viewer
}

void Footage::AddedToGraphEvent(Project *p)
{
	connect(p->color_manager(), &ColorManager::default_input_changed, this,
			&Footage::default_color_space_changed);
	if (ProxyManager::instance()) {
		connect(ProxyManager::instance(), &ProxyManager::proxy_ready, this,
				&Footage::proxy_ready);
		connect(ProxyManager::instance(), &ProxyManager::proxy_finished, this,
				&Footage::proxy_finished);
	}
}

void Footage::RemovedFromGraphEvent(Project *p)
{
	disconnect(p->color_manager(), &ColorManager::default_input_changed, this,
			   &Footage::default_color_space_changed);
	if (ProxyManager::instance()) {
		disconnect(ProxyManager::instance(), &ProxyManager::proxy_ready, this,
				   &Footage::proxy_ready);
		disconnect(ProxyManager::instance(), &ProxyManager::proxy_finished, this,
				   &Footage::proxy_finished);
	}
}

void Footage::reprobe()
{
	// Determine if file still exists
	QString filename = this->filename();

	// In case of failure to load file, set timestamp to a value that will always be invalid so we
	// continuously reprobe
	set_timestamp(0);

	if (!filename.isEmpty()) {
		QFileInfo info(filename);

		if (info.exists()) {
			// Grab timestamp
			set_timestamp(info.lastModified().toMSecsSinceEpoch());

			// Determine if we've already cached the metadata of this file
			QString meta_cache_file =
				QDir(QStandardPaths::writableLocation(
						 QStandardPaths::CacheLocation))
					.filePath(FileFunctions::get_unique_file_identifier(filename));

			FootageDescription footage_info;

			// Try to load footage info from cache
			if (!QFileInfo::exists(meta_cache_file) ||
				!footage_info.load(meta_cache_file)) {
				// Probe and create cache
				QVector<DecoderPtr> decoder_list =
					Decoder::receive_list_of_all_decoders();

				foreach (DecoderPtr decoder, decoder_list) {
					footage_info = decoder->probe(filename, cancelled_);

					if (footage_info.is_valid()) {
						break;
					}
				}

				if (!cancelled_ || !cancelled_->heard_cancel()) {
					// Only cache successful probes; caching a failed probe
					// would make every future load re-use the invalid metadata
					if (footage_info.is_valid() &&
						!footage_info.save(meta_cache_file)) {
						qWarning()
							<< "Failed to save stream cache, footage will have to be re-probed";
					}
				}
			}

			if (footage_info.is_valid()) {
				decoder_ = footage_info.decoder();

				input_array_resize(k_video_params_input,
								 footage_info.get_video_streams().size());
				for (int i = 0; i < footage_info.get_video_streams().size();
					 i++) {
					VideoParams video_stream =
						footage_info.get_video_streams().at(i);

					if (i < input_array_size(k_video_params_input)) {
						VideoParams existing = this->get_video_params(i);
						if (existing.is_valid()) {
							video_stream =
								merge_video_stream(video_stream, existing);
						}
					}

					set_stream(Track::k_video, QVariant::fromValue(video_stream),
							  i);
				}

				input_array_resize(k_audio_params_input,
								 footage_info.get_audio_streams().size());
				for (int i = 0; i < footage_info.get_audio_streams().size();
					 i++) {
					set_stream(Track::k_audio,
							  QVariant::fromValue(
								  footage_info.get_audio_streams().at(i)),
							  i);
				}

				input_array_resize(k_subtitle_params_input,
								 footage_info.get_subtitle_streams().size());
				for (int i = 0; i < footage_info.get_subtitle_streams().size();
					 i++) {
					set_stream(Track::k_subtitle,
							  QVariant::fromValue(
								  footage_info.get_subtitle_streams().at(i)),
							  i);
				}

				total_stream_count_ = footage_info.get_stream_count();
				if (footage_info.has_source_start_time()) {
					set_source_start_time(footage_info.source_start_time(),
									   footage_info.source_start_time_source());
				}

				set_valid();
			}
		}
	}
}

VideoParams Footage::merge_video_stream(const VideoParams &base,
									  const VideoParams &over)
{
	VideoParams merged = base;

	merged.set_pixel_aspect_ratio(over.pixel_aspect_ratio());
	merged.set_interlacing(over.interlacing());
	merged.set_colorspace(over.colorspace());
	merged.set_premultiplied_alpha(over.premultiplied_alpha());
	merged.set_video_type(over.video_type());
	merged.set_color_range(over.color_range());
	if (merged.video_type() == VideoParams::k_video_type_image_sequence) {
		merged.set_start_time(over.start_time());
		merged.set_duration(over.duration());
		merged.set_frame_rate(over.frame_rate());
		merged.set_time_base(over.time_base());
	}

	return merged;
}

void Footage::check_footage()
{
	// Don't check files if not the active window
	if (qApp->activeWindow()) {
		QString fn = filename();

		if (!fn.isEmpty()) {
			QFileInfo info(fn);

			qint64 current_file_timestamp;
			if (!info.lastModified().isValid()) {
				current_file_timestamp = 0;
			} else {
				current_file_timestamp =
					info.lastModified().toMSecsSinceEpoch();
			}

			if (current_file_timestamp != timestamp()) {
				// File has changed!
				clear();
				reprobe();
				invalidate_all(k_filename_input);
			}
		}
	}
}

void Footage::default_color_space_changed()
{
	bool inv = false;
	int sz = get_video_stream_count();
	for (int i = 0; i < sz; i++) {
		// Check if any of our streams are using the default colorspace
		if (get_video_params(i).colorspace().isEmpty()) {
			inv = true;
			break;
		}
	}

	if (inv) {
		invalidate_all(k_video_params_input);
	}
}

void Footage::proxy_ready(const QString &source_filename, int stream_index,
						 const QString &proxy_filename)
{
	proxy_finished(source_filename, stream_index, proxy_filename,
				  ProxyManager::k_proxy_ready);
}

void Footage::proxy_finished(const QString &source_filename, int stream_index,
							const QString &proxy_filename,
							ProxyManager::ProxyState state)
{
	if (filename() != source_filename ||
		proxy_video_stream_index_ != stream_index ||
		proxy_path_ != proxy_filename) {
		return;
	}

	proxy_state_ = state;
	invalidate_all(k_filename_input);
}

}
