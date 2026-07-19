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

#include "config.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QXmlStreamWriter>

#include "codec/exportformat.h"
#include "common/autoscroll.h"
#include "common/filefunctions.h"
#include "common/xmlutils.h"
#include "core.h"
#include "timeline/timelinecommon.h"
#include "ui/colorcoding.h"
#include "ui/style/style.h"
#include "widget/timelinewidget/tool/import.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

Config Config::current_config;

Config::Config()
{
	set_defaults();
}

void Config::set_entry_internal(const QString &key, NodeValue::Type type,
							  const QVariant &data)
{
	config_map_[key] = { type, data };
}

QString Config::get_config_file_path()
{
	return QDir(FileFunctions::get_configuration_location())
		.filePath(QStringLiteral("config.xml"));
}

Config &Config::current()
{
	return current_config;
}

void Config::set_defaults()
{
	config_map_.clear();
	set_entry_internal(QStringLiteral("Style"), NodeValue::k_text,
					 StyleManager::k_default_style);
	set_entry_internal(QStringLiteral("TimecodeDisplay"), NodeValue::k_int,
					 Timecode::k_timecode_drop_frame);
	set_entry_internal(QStringLiteral("DefaultStillLength"), NodeValue::k_rational,
					 QVariant::fromValue(Rational(2)));
	set_entry_internal(QStringLiteral("HoverFocus"), NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("AudioScrubbing"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("AutorecoveryEnabled"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("AutorecoveryInterval"), NodeValue::k_int,
					 1);
	set_entry_internal(QStringLiteral("AutorecoveryMaximum"), NodeValue::k_int,
					 20);
	set_entry_internal(QStringLiteral("DiskCacheSaveInterval"), NodeValue::k_int,
					 10000);
	set_entry_internal(QStringLiteral("Language"), NodeValue::k_text, QString());
	set_entry_internal(QStringLiteral("ScrollZooms"), NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("EnableSeekToImport"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("EditToolAlsoSeeks"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("EditToolSelectsLinks"),
					 NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("EnableDragFilesToTimeline"),
					 NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("InvertTimelineScrollAxes"),
					 NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("SelectAlsoSeeks"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("PasteSeeks"), NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("SeekAlsoSelects"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("SetNameWithMarker"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("AutoSeekToBeginning"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("DropFileOnMediaToReplace"),
					 NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("AddDefaultEffectsToClips"),
					 NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("AutoscaleByDefault"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("Autoscroll"), NodeValue::k_int,
					 AutoScroll::k_page);
	set_entry_internal(QStringLiteral("AutoSelectDivider"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("SetNameWithMarker"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("RectifiedWaveforms"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("DropWithoutSequenceBehavior"),
					 NodeValue::k_int, ImportTool::k_dws_ask);
	set_entry_internal(QStringLiteral("Loop"), NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("SplitClipsCopyNodes"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("UseGradients"), NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("AutoMergeTracks"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("UseSliderLadders"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("ShowWelcomeDialog"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("ShowClipWhileDragging"),
					 NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("StopPlaybackOnLastFrame"),
					 NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("UseLegacyColorInInputTab"),
					 NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("ReassocLinToNonLin"), NodeValue::k_boolean,
					 false);
	set_entry_internal(QStringLiteral("PreviewNonFloatDontAskAgain"),
					 NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("UseGLFinish"), NodeValue::k_boolean, false);
	set_entry_internal(QStringLiteral("GraphicsBackend"), NodeValue::k_text,
					 QStringLiteral("opengl"));

	set_entry_internal(QStringLiteral("TimelineThumbnailMode"), NodeValue::k_int,
					 Timeline::k_thumbnail_in_out);
	set_entry_internal(QStringLiteral("TimelineWaveformMode"), NodeValue::k_int,
					 Timeline::k_waveforms_enabled);

	set_entry_internal(
		QStringLiteral("DefaultVideoTransition"), NodeValue::k_text,
		QStringLiteral("org.olivevideoeditor.Olive.crossdissolve"));
	set_entry_internal(
		QStringLiteral("DefaultAudioTransition"), NodeValue::k_text,
		QStringLiteral("org.olivevideoeditor.Olive.crossdissolve"));
	set_entry_internal(QStringLiteral("DefaultTransitionLength"),
					 NodeValue::k_rational, QVariant::fromValue(Rational(1)));

	set_entry_internal(QStringLiteral("DefaultSubtitleSize"), NodeValue::k_int,
					 48);
	set_entry_internal(QStringLiteral("DefaultSubtitleFamily"), NodeValue::k_text,
					 QString());
	set_entry_internal(QStringLiteral("DefaultSubtitleWeight"), NodeValue::k_int,
					 QFont::Bold);
	set_entry_internal(QStringLiteral("AntialiasSubtitles"), NodeValue::k_boolean,
					 true);

	set_entry_internal(QStringLiteral("AutoCacheDelay"), NodeValue::k_int, 1000);

	set_entry_internal(QStringLiteral("CatColor0"), NodeValue::k_int,
					 ColorCoding::k_red);
	set_entry_internal(QStringLiteral("CatColor1"), NodeValue::k_int,
					 ColorCoding::k_maroon);
	set_entry_internal(QStringLiteral("CatColor2"), NodeValue::k_int,
					 ColorCoding::k_orange);
	set_entry_internal(QStringLiteral("CatColor3"), NodeValue::k_int,
					 ColorCoding::k_brown);
	set_entry_internal(QStringLiteral("CatColor4"), NodeValue::k_int,
					 ColorCoding::k_yellow);
	set_entry_internal(QStringLiteral("CatColor5"), NodeValue::k_int,
					 ColorCoding::k_olive);
	set_entry_internal(QStringLiteral("CatColor6"), NodeValue::k_int,
					 ColorCoding::k_lime);
	set_entry_internal(QStringLiteral("CatColor7"), NodeValue::k_int,
					 ColorCoding::k_green);
	set_entry_internal(QStringLiteral("CatColor8"), NodeValue::k_int,
					 ColorCoding::k_cyan);
	set_entry_internal(QStringLiteral("CatColor9"), NodeValue::k_int,
					 ColorCoding::k_teal);
	set_entry_internal(QStringLiteral("CatColor10"), NodeValue::k_int,
					 ColorCoding::k_blue);
	set_entry_internal(QStringLiteral("CatColor11"), NodeValue::k_int,
					 ColorCoding::k_navy);

	set_entry_internal(QStringLiteral("AudioOutput"), NodeValue::k_text,
					 QString());
	set_entry_internal(QStringLiteral("AudioInput"), NodeValue::k_text, QString());

	set_entry_internal(QStringLiteral("AudioOutputSampleRate"), NodeValue::k_int,
					 48000);
	set_entry_internal(QStringLiteral("AudioOutputChannelLayout"),
					 NodeValue::k_int,
					 QVariant::fromValue(static_cast<int64_t>(k_channel_layout_stereo)));
	set_entry_internal(
		QStringLiteral("AudioOutputSampleFormat"), NodeValue::k_text,
		QString::fromStdString(SampleFormat(SampleFormat::s16).to_string()));

	set_entry_internal(QStringLiteral("AudioRecordingFormat"), NodeValue::k_int,
					 ExportFormat::k_format_wav);
	set_entry_internal(QStringLiteral("AudioRecordingCodec"), NodeValue::k_int,
					 ExportCodec::k_codec_pcm);
	set_entry_internal(QStringLiteral("AudioRecordingSampleRate"),
					 NodeValue::k_int, 48000);
	set_entry_internal(QStringLiteral("AudioRecordingChannelLayout"),
					 NodeValue::k_int,
					 QVariant::fromValue(static_cast<int64_t>(k_channel_layout_stereo)));
	set_entry_internal(
		QStringLiteral("AudioRecordingSampleFormat"), NodeValue::k_text,
		QString::fromStdString(SampleFormat(SampleFormat::s16).to_string()));
	set_entry_internal(QStringLiteral("AudioRecordingBitRate"), NodeValue::k_int,
					 320);

	set_entry_internal(QStringLiteral("DiskCacheBehind"), NodeValue::k_rational,
					 QVariant::fromValue(Rational(0)));
	set_entry_internal(QStringLiteral("DiskCacheAhead"), NodeValue::k_rational,
					 QVariant::fromValue(Rational(60)));

	set_entry_internal(QStringLiteral("ProxyWidth"), NodeValue::k_int, 1280);
	set_entry_internal(QStringLiteral("ProxyHeight"), NodeValue::k_int, 720);
	set_entry_internal(QStringLiteral("ProxyCRF"), NodeValue::k_int, 23);
	set_entry_internal(QStringLiteral("ProxyPreset"), NodeValue::k_text,
					 QStringLiteral("veryfast"));
	set_entry_internal(QStringLiteral("ProxyIncludeAudio"), NodeValue::k_boolean,
					 true);
	set_entry_internal(QStringLiteral("FFmpegPath"), NodeValue::k_text,
					 QString());

	set_entry_internal(QStringLiteral("LUTLibraryPaths"), NodeValue::k_text,
					 QString());

	set_entry_internal(QStringLiteral("DefaultSequenceWidth"), NodeValue::k_int,
					 1920);
	set_entry_internal(QStringLiteral("DefaultSequenceHeight"), NodeValue::k_int,
					 1080);
	set_entry_internal(QStringLiteral("DefaultSequencePixelAspect"),
					 NodeValue::k_rational, QVariant::fromValue(Rational(1)));
	set_entry_internal(QStringLiteral("DefaultSequenceFrameRate"),
					 NodeValue::k_rational,
					 QVariant::fromValue(Rational(1001, 30000)));
	set_entry_internal(QStringLiteral("DefaultSequenceInterlacing"),
					 NodeValue::k_int, VideoParams::k_interlace_none);
	set_entry_internal(QStringLiteral("DefaultSequenceAutoCache2"),
					 NodeValue::k_boolean, true);
	set_entry_internal(QStringLiteral("DefaultSequenceAudioFrequency"),
					 NodeValue::k_int, 48000);
	set_entry_internal(
		QStringLiteral("DefaultSequenceAudioLayout"), NodeValue::k_int,
		QVariant::fromValue(static_cast<int64_t>(k_channel_layout_stereo)));

	// Online/offline settings
	set_entry_internal(QStringLiteral("OnlinePixelFormat"), NodeValue::k_int,
					 PixelFormat::f32);
	set_entry_internal(QStringLiteral("OfflinePixelFormat"), NodeValue::k_int,
					 PixelFormat::f32);

	set_entry_internal(QStringLiteral("MarkerColor"), NodeValue::k_int,
					 ColorCoding::k_lime);
}

void Config::load()
{
	QFile config_file(get_config_file_path());

	if (!config_file.exists()) {
		return;
	}

	if (!config_file.open(QFile::ReadOnly)) {
		qWarning()
			<< "Failed to load application settings. This session will use defaults.";
		return;
	}

	// Reset to defaults
	current_config.set_defaults();

	QXmlStreamReader reader(&config_file);

	QString config_version;

	while (xml_read_next_start_element(&reader)) {
		if (reader.name() == QStringLiteral("Configuration")) {
			while (xml_read_next_start_element(&reader)) {
				QString key = reader.name().toString();
				QString value = reader.readElementText();

				if (key == QStringLiteral("Version")) {
					config_version = value;

					if (!value.contains(".")) {
						qDebug()
							<< "CONFIG: This is a 0.1.x config file, upconvert";
					}
				} else if (key == QStringLiteral("DefaultSequenceFrameRate") &&
						   !config_version.contains('.')) {
					// 0.1.x stored this value as a float while we now use rationals, we'll use a heuristic to find the closest
					// supported Rational
					qDebug() << "  CONFIG: Finding closest match to" << value;

					double config_fr = value.toDouble();

					const QVector<Rational> &supported_frame_rates =
						VideoParams::k_supported_frame_rates;

					Rational match = supported_frame_rates.first();
					double match_diff = qAbs(match.to_double() - config_fr);

					for (int i = 1; i < supported_frame_rates.size(); i++) {
						double diff = qAbs(
							supported_frame_rates.at(i).to_double() - config_fr);

						if (diff < match_diff) {
							match = supported_frame_rates.at(i);
							match_diff = diff;
						}
					}

					qDebug()
						<< "  CONFIG: Closest match was" << match.to_double();

					current_config[key] = QVariant::fromValue(match.flipped());
				} else {
					current_config[key] = NodeValue::string_to_value(
						current_config.get_config_entry_type(key), value, false);
				}
			}

			//reader.skipCurrentElement();
		} else {
			reader.skipCurrentElement();
		}
	}

	if (reader.hasError()) {
		// Config::Load() is called before Core (and therefore the main window)
		// is constructed, so we cannot use Core::instance()->main_window() as
		// the message box parent. Passing nullptr creates a top-level dialog.
		QWidget *parent = Core::instance() ? Core::instance()->main_window() :
											 nullptr;
		QMessageBox::critical(
			parent,
			QCoreApplication::translate("Config", "Error loading settings"),
			QCoreApplication::translate(
				"Config",
				"Failed to load application settings. This session will "
				"use defaults.\n\n%1")
				.arg(reader.errorString()),
			QMessageBox::Ok);
		current_config.set_defaults();
	}

	config_file.close();
}

void Config::save()
{
	QString real_filename = get_config_file_path();
	QString temp_filename =
		FileFunctions::get_safe_temporary_filename(real_filename);

	QFile config_file(temp_filename);

	if (!config_file.open(QFile::WriteOnly)) {
		QMessageBox::critical(
			Core::instance()->main_window(),
			QCoreApplication::translate("Config", "Error saving settings"),
			QCoreApplication::translate(
				"Config",
				"Failed to save application settings. The application "
				"may lack write permissions for this location."),
			QMessageBox::Ok);
		return;
	}

	QXmlStreamWriter writer(&config_file);
	writer.setAutoFormatting(true);

	writer.writeStartDocument();

	writer.writeStartElement("Configuration");

	// Anything after the hyphen is considered "unimportant" information
	writer.writeTextElement(
		"Version", QCoreApplication::applicationVersion().split('-').first());

	QMapIterator<QString, ConfigEntry> iterator(current_config.config_map_);
	while (iterator.hasNext()) {
		iterator.next();

		QString value = NodeValue::value_to_string(iterator.value().type,
												 iterator.value().data, false);

		if (iterator.value().type == NodeValue::k_none) {
			qWarning() << "Config key" << iterator.key()
					   << "had null type and was discarded";
		} else {
			writer.writeTextElement(iterator.key(), value);
		}
	}

	writer.writeEndElement(); // Configuration

	writer.writeEndDocument();

	config_file.close();

	if (!FileFunctions::rename_file_allow_overwrite(temp_filename,
												 real_filename)) {
		qWarning()
			<< QStringLiteral(
				   "Failed to overwrite \"%1\". Config has been saved as \"%2\" instead.")
				   .arg(real_filename, temp_filename);
	}
}

QVariant Config::operator[](const QString &key) const
{
	return config_map_[key].data;
}

QVariant &Config::operator[](const QString &key)
{
	return config_map_[key].data;
}

NodeValue::Type Config::get_config_entry_type(const QString &key) const
{
	return config_map_[key].type;
}

}
