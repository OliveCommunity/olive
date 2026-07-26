#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>

#include "codec/encoder.h"
#include "oakengine/encoding.h"
#include "dialog/export/codec/h264section.h"
#include "dialog/export/codec/imagesection.h"
#include "dialog/export/exportadvancedvideodialog.h"
#include "dialog/export/exportaudiotab.h"
#include "dialog/export/exportformatcombobox.h"
#include "dialog/export/exportsavepresetdialog.h"
#include "dialog/export/exportsubtitlestab.h"
#include "dialog/export/exportvideotab.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "widget/manageddisplay/colorprocessorhandle.h"

namespace
{

// Redirects QStandardPaths (used for the export preset directory) to a
// disposable test location for the lifetime of the guard
class StandardPathsTestModeGuard {
public:
	StandardPathsTestModeGuard()
	{
		QStandardPaths::setTestModeEnabled(true);
	}

	~StandardPathsTestModeGuard()
	{
		QStandardPaths::setTestModeEnabled(false);
	}
};

QList<int> menu_format_data(const olive::ExportFormatComboBox &combo)
{
	QList<int> formats;
	// custom_menu_ is an olive::Menu (a QMenu without its own Q_OBJECT)
	auto *menu = combo.findChild<QMenu *>();
	if (!menu) {
		return formats;
	}
	foreach (QAction *a, menu->actions()) {
		if (!a->isSeparator() && a->data().isValid()) {
			formats.append(a->data().toInt());
		}
	}
	return formats;
}

} // namespace

//
// export: format combobox
//
TEST(DialogExportFormatComboBox, GetSetFormatRoundTrip)
{
	olive::ExportFormatComboBox combo;

	// Before any selection the format is the invalid placeholder
	EXPECT_EQ(combo.get_format(), olive::ExportFormat::k_format_count);

	combo.set_format(olive::ExportFormat::k_format_matroska);
	EXPECT_EQ(combo.get_format(), olive::ExportFormat::k_format_matroska);
	EXPECT_EQ(combo.currentText(),
			  olive::ExportFormat::get_name(olive::ExportFormat::k_format_matroska));
}

TEST(DialogExportFormatComboBox, MenuSelectionEmitsFormatChanged)
{
	olive::ExportFormatComboBox combo;
	QSignalSpy spy(&combo, &olive::ExportFormatComboBox::format_changed);

	QAction action(QStringLiteral("QuickTime"), &combo);
	action.setData(static_cast<int>(olive::ExportFormat::k_format_quick_time));

	QMetaObject::invokeMethod(&combo, "handle_index_change",
							  Q_ARG(QAction *, &action));

	EXPECT_EQ(combo.get_format(), olive::ExportFormat::k_format_quick_time);
	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().first().toInt(),
			  static_cast<int>(olive::ExportFormat::k_format_quick_time));
}

TEST(DialogExportFormatComboBox, AudioOnlyModeListsOnlyAudioFormats)
{
	olive::ExportFormatComboBox combo(
		olive::ExportFormatComboBox::k_show_audio_only);

	const QList<int> formats = menu_format_data(combo);
	EXPECT_FALSE(formats.isEmpty());

	foreach (int f, formats) {
		const auto fmt = static_cast<olive::ExportFormat::Format>(f);
		EXPECT_TRUE(olive::ExportFormat::get_video_codecs(fmt).isEmpty())
			<< "Format " << f << " should not have video codecs";
		EXPECT_FALSE(olive::ExportFormat::get_audio_codecs(fmt).isEmpty())
			<< "Format " << f << " should have audio codecs";
	}

	EXPECT_TRUE(formats.contains(
		static_cast<int>(olive::ExportFormat::k_format_wav)));
	EXPECT_FALSE(formats.contains(
		static_cast<int>(olive::ExportFormat::k_format_matroska)));
}

//
// export: audio tab
//
TEST(DialogExportAudioTab, SetFormatPopulatesCodecs)
{
	olive::ExportAudioTab tab;

	const QList<olive::ExportCodec::Codec> codecs =
		olive::ExportFormat::get_audio_codecs(olive::ExportFormat::k_format_matroska);
	ASSERT_FALSE(codecs.isEmpty());

	EXPECT_EQ(tab.set_format(olive::ExportFormat::k_format_matroska),
			  codecs.size());

	// The first codec is auto-selected
	EXPECT_EQ(tab.get_codec(), codecs.first());
}

TEST(DialogExportAudioTab, LosslessCodecDisablesBitRate)
{
	olive::ExportAudioTab tab;
	tab.set_format(olive::ExportFormat::k_format_matroska);

	tab.set_codec(olive::ExportCodec::k_codec_aac);
	EXPECT_TRUE(tab.bit_rate_slider()->isEnabled());
	EXPECT_EQ(tab.get_codec(), olive::ExportCodec::k_codec_aac);

	// PCM is lossless, so no bit rate setting applies
	tab.set_codec(olive::ExportCodec::k_codec_pcm);
	EXPECT_EQ(tab.get_codec(), olive::ExportCodec::k_codec_pcm);
	EXPECT_FALSE(tab.bit_rate_slider()->isEnabled());
	EXPECT_TRUE(tab.bit_rate_slider()->is_tristate());
}

TEST(DialogExportAudioTab, FormatWithoutAudioCodecsDisablesTab)
{
	olive::ExportAudioTab tab;

	// PNG carries no audio
	EXPECT_EQ(tab.set_format(olive::ExportFormat::k_format_png), 0);
	EXPECT_FALSE(tab.isEnabled());
}

//
// export: subtitles tab
//
TEST(DialogExportSubtitlesTab, SidecarStateFollowsFormatCapabilities)
{
	olive::ExportSubtitlesTab tab;
	tab.set_sidecar_format(olive::ExportFormat::k_format_srt);

	auto *sidecar_box = tab.findChild<QCheckBox *>();
	ASSERT_NE(sidecar_box, nullptr);

	// Matroska can embed subtitles: sidecar is optional and off by default
	tab.set_format(olive::ExportFormat::k_format_matroska);
	EXPECT_TRUE(sidecar_box->isEnabled());
	EXPECT_FALSE(tab.get_sidecar_enabled());
	EXPECT_EQ(tab.get_subtitle_codec(), olive::ExportCodec::k_codec_srt);

	// SetSidecarEnabled toggles the check state (used to restore params)
	tab.set_sidecar_enabled(true);
	EXPECT_TRUE(tab.get_sidecar_enabled());
	tab.set_sidecar_enabled(false);
	EXPECT_FALSE(tab.get_sidecar_enabled());

	// SRT is a subtitles-only format: sidecar makes no sense, forced off
	tab.set_format(olive::ExportFormat::k_format_srt);
	EXPECT_FALSE(sidecar_box->isEnabled());
	EXPECT_FALSE(tab.get_sidecar_enabled());

	// WAV cannot carry subtitles at all: sidecar is forced on
	tab.set_format(olive::ExportFormat::k_format_wav);
	EXPECT_FALSE(sidecar_box->isEnabled());
	EXPECT_TRUE(tab.get_sidecar_enabled());
}

//
// export: video tab
//
TEST(DialogExportVideoTab, SetFormatPopulatesCodecs)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	olive::ExportVideoTab tab(oak_color_manager(project.color_manager()));

	const QList<olive::ExportCodec::Codec> codecs =
		olive::ExportFormat::get_video_codecs(olive::ExportFormat::k_format_matroska);
	ASSERT_FALSE(codecs.isEmpty());

	EXPECT_EQ(tab.set_format(olive::ExportFormat::k_format_matroska),
			  codecs.size());
	EXPECT_EQ(tab.get_selected_codec(), codecs.first());

	tab.set_selected_codec(olive::ExportCodec::k_codec_h265);
	EXPECT_EQ(tab.get_selected_codec(), olive::ExportCodec::k_codec_h265);
}

TEST(DialogExportVideoTab, CodecSelectsMatchingSection)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	olive::ExportVideoTab tab(oak_color_manager(project.color_manager()));
	tab.set_format(olive::ExportFormat::k_format_matroska);

	// First Matroska codec is H.264, which has a dedicated section
	tab.video_codec_changed();
	EXPECT_NE(tab.get_codec_section(), nullptr);

	// Still image codecs get the image section instead
	tab.set_format(olive::ExportFormat::k_format_png);
	tab.video_codec_changed();
	EXPECT_NE(dynamic_cast<olive::ImageSection *>(tab.get_codec_section()),
			  nullptr);
}

TEST(DialogExportVideoTab, ImageSequenceCheckboxRoundTrips)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	olive::ExportVideoTab tab(oak_color_manager(project.color_manager()));
	tab.set_format(olive::ExportFormat::k_format_png);
	tab.video_codec_changed();

	tab.set_image_sequence(true);
	EXPECT_TRUE(tab.is_image_sequence_set());

	tab.set_image_sequence(false);
	EXPECT_FALSE(tab.is_image_sequence_set());
}

TEST(DialogExportVideoTab, MaintainAspectTogglesScalingMethod)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	olive::ExportVideoTab tab(oak_color_manager(project.color_manager()));

	tab.maintain_aspect_checkbox()->setChecked(true);
	EXPECT_FALSE(tab.scaling_method_combobox()->isEnabled());

	tab.maintain_aspect_checkbox()->setChecked(false);
	EXPECT_TRUE(tab.scaling_method_combobox()->isEnabled());
}

//
// export: codec sections
//
TEST(DialogExportH264CRFSection, ValueRoundTripsAndClamps)
{
	olive::H264CRFSection section(olive::H264CRFSection::k_default_h264_crf);

	EXPECT_EQ(section.get_value(), olive::H264CRFSection::k_default_h264_crf);

	section.set_value(30);
	EXPECT_EQ(section.get_value(), 30);

	section.set_value(99);
	EXPECT_EQ(section.get_value(), 51);

	section.set_value(-5);
	EXPECT_EQ(section.get_value(), 0);
}

TEST(DialogExportH264BitRateSection, BitRateRoundTripsInBits)
{
	olive::H264BitRateSection section;

	section.set_target_bit_rate(8000000);
	EXPECT_EQ(section.get_target_bit_rate(), 8000000);

	section.set_maximum_bit_rate(16000000);
	EXPECT_EQ(section.get_maximum_bit_rate(), 16000000);
}

//
// export: advanced video dialog
//
TEST(DialogExportAdvancedVideo, FieldsRoundTrip)
{
	olive::ExportAdvancedVideoDialog dialog({ QStringLiteral("yuv420p"),
											  QStringLiteral("yuv422p") });

	dialog.set_threads(4);
	EXPECT_EQ(dialog.threads(), 4);

	dialog.set_pix_fmt(QStringLiteral("yuv422p"));
	EXPECT_EQ(dialog.pix_fmt(), QStringLiteral("yuv422p"));

	dialog.set_yuv_range(olive::VideoParams::k_color_range_full);
	EXPECT_EQ(dialog.yuv_range(), olive::VideoParams::k_color_range_full);
}

//
// export: save preset dialog
//
TEST(DialogExportSavePreset, AcceptWritesPresetFile)
{
	StandardPathsTestModeGuard test_mode;

	OakEngineEncodingParams *params = oakengine_encoding_params_create();

	olive::ExportSavePresetDialog dialog(params);

	auto *name_edit = dialog.findChild<QLineEdit *>();
	ASSERT_NE(name_edit, nullptr);
	name_edit->setText(QStringLiteral("oak-test-preset"));

	EXPECT_EQ(dialog.get_selected_preset_name(),
			  QStringLiteral("oak-test-preset"));

	dialog.accept();
	EXPECT_EQ(dialog.result(), QDialog::Accepted);

	EXPECT_TRUE(olive::EncodingParams::get_list_of_presets().contains(
		QStringLiteral("oak-test-preset")));

	// Clean up the preset file written to the test config location
	QFile::remove(QDir(olive::EncodingParams::get_preset_path())
					  .filePath(QStringLiteral("oak-test-preset")));
}
