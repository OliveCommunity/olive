#include <gtest/gtest.h>

#include <memory>

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "common/keyframetypes.h"
#include "core.h"
#include "oakengine/app.h"
#include "undo/undostack.h"
#include "dialog/footageproperties/footageproperties.h"
#include "dialog/footagerelink/footagerelinkdialog.h"
#include "dialog/keyframeproperties/keyframeproperties.h"
#include "dialog/markerproperties/markerpropertiesdialog.h"
#include "dialog/projectproperties/projectproperties.h"
#include "dialog/sequence/sequence.h"
#include "dialog/sequence/sequencedialogparametertab.h"
#include "dialog/sequence/sequencedialogpresettab.h"
#include "dialog/sequence/sequencepreset.h"
#include "dialog/speedduration/speeddurationdialog.h"
#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/math/math/math.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "render/diskmanager.h"
#include "timeline/timelinemarker.h"
#include "widget/colorlabelmenu/colorcodingcombobox.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/rationalslider.h"

namespace
{

// Several of these dialogs push undo commands to the global undo stack on
// accept(), which requires the Core singleton (see project_factory_test.cpp)
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

void clear_undo_stack()
{
	// The process-wide undo stack previously reached via Core::undo_stack()
	if (auto *stack =
			static_cast<olive::UndoStack *>(oakengine_app_undo_stack())) {
		stack->clear();
	}
}

std::unique_ptr<olive::Project> create_project()
{
	olive::ColorManager::set_up_default_config();

	auto project = std::make_unique<olive::Project>();
	project->initialize();
	return project;
}

// Redirects QStandardPaths (used for the sequence preset file, which
// PresetManager rewrites on destruction) to a disposable test location
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

olive::ClipBlock *create_clip(olive::Project *project,
							 const olive::Rational &length)
{
	auto *clip = new olive::ClipBlock();
	clip->setParent(project);
	clip->set_length_and_media_out(length);
	return clip;
}

olive::Track *create_track_with_clip(olive::Project *project,
								  olive::ClipBlock *clip)
{
	auto *track = new olive::Track();
	track->setParent(project);
	track->append_block(clip);
	return track;
}

} // namespace

//
// speedduration
//
TEST(DialogSpeedDuration, InitialValuesReflectSingleClip)
{
	ensure_app_singletons();
	auto project = create_project();
	auto *clip = create_clip(project.get(), olive::Rational(4));
	create_track_with_clip(project.get(), clip);

	olive::SpeedDurationDialog dialog({ reinterpret_cast<OakEngineBlock *>(clip) }, olive::Rational(1, 24));

	auto *speed_slider = dialog.findChild<olive::FloatSlider *>();
	auto *dur_slider = dialog.findChild<olive::RationalSlider *>();
	ASSERT_NE(speed_slider, nullptr);
	ASSERT_NE(dur_slider, nullptr);

	EXPECT_DOUBLE_EQ(speed_slider->get_value(), 1.0);
	EXPECT_EQ(dur_slider->get_value(), olive::Rational(4));
	EXPECT_FALSE(speed_slider->is_tristate());
	EXPECT_FALSE(dur_slider->is_tristate());
}

TEST(DialogSpeedDuration, LinkedSpeedChangeUpdatesDuration)
{
	ensure_app_singletons();
	auto project = create_project();
	auto *clip = create_clip(project.get(), olive::Rational(4));
	create_track_with_clip(project.get(), clip);

	olive::SpeedDurationDialog dialog({ reinterpret_cast<OakEngineBlock *>(clip) }, olive::Rational(1, 24));

	auto *speed_slider = dialog.findChild<olive::FloatSlider *>();
	auto *dur_slider = dialog.findChild<olive::RationalSlider *>();

	// Programmatic SetValue() does not emit ValueChanged (only user edits
	// do), so emit the signal explicitly to drive the linked update
	speed_slider->set_value(2.0);
	emit speed_slider->value_changed(2.0);
	EXPECT_EQ(dur_slider->get_value(), olive::Rational(2));

	speed_slider->set_value(0.5);
	emit speed_slider->value_changed(0.5);
	EXPECT_EQ(dur_slider->get_value(), olive::Rational(8));
}

TEST(DialogSpeedDuration, LinkedDurationChangeUpdatesSpeed)
{
	ensure_app_singletons();
	auto project = create_project();
	auto *clip = create_clip(project.get(), olive::Rational(4));
	create_track_with_clip(project.get(), clip);

	olive::SpeedDurationDialog dialog({ reinterpret_cast<OakEngineBlock *>(clip) }, olive::Rational(1, 24));

	auto *speed_slider = dialog.findChild<olive::FloatSlider *>();
	auto *dur_slider = dialog.findChild<olive::RationalSlider *>();

	// Programmatic SetValue() does not emit ValueChanged (only user edits
	// do), so emit the signal explicitly to drive the linked update
	dur_slider->set_value(olive::Rational(2));
	emit dur_slider->value_changed(olive::Rational(2));
	EXPECT_DOUBLE_EQ(speed_slider->get_value(), 2.0);

	dur_slider->set_value(olive::Rational(16));
	emit dur_slider->value_changed(olive::Rational(16));
	EXPECT_DOUBLE_EQ(speed_slider->get_value(), 0.25);
}

TEST(DialogSpeedDuration, AcceptAppliesSpeedAndLength)
{
	ensure_app_singletons();
	auto project = create_project();
	auto *clip = create_clip(project.get(), olive::Rational(4));
	create_track_with_clip(project.get(), clip);

	{
		olive::SpeedDurationDialog dialog({ reinterpret_cast<OakEngineBlock *>(clip) }, olive::Rational(1, 24));

		// Doubling the speed with the link checked halves the duration
		auto *speed_slider = dialog.findChild<olive::FloatSlider *>();
		speed_slider->set_value(2.0);
		emit speed_slider->value_changed(2.0);

		dialog.accept();
	}

	EXPECT_DOUBLE_EQ(clip->speed(), 2.0);
	EXPECT_EQ(clip->length(), olive::Rational(2));

	clear_undo_stack();
}

TEST(DialogSpeedDuration, DifferingSpeedsAcrossClipsProduceTristate)
{
	ensure_app_singletons();
	auto project = create_project();
	auto *clip_a = create_clip(project.get(), olive::Rational(4));
	auto *clip_b = create_clip(project.get(), olive::Rational(4));
	create_track_with_clip(project.get(), clip_a);
	clip_b->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	create_track_with_clip(project.get(), clip_b);

	olive::SpeedDurationDialog dialog({ reinterpret_cast<OakEngineBlock *>(clip_a), reinterpret_cast<OakEngineBlock *>(clip_b) },
									  olive::Rational(1, 24));

	EXPECT_TRUE(dialog.findChild<olive::FloatSlider *>()->is_tristate());
	// Durations are identical, so the duration slider must not be tristate
	EXPECT_FALSE(dialog.findChild<olive::RationalSlider *>()->is_tristate());
}

TEST(DialogSpeedDuration, AcceptDerivesPerClipSpeedFromDuration)
{
	ensure_app_singletons();
	auto project = create_project();
	auto *clip_a = create_clip(project.get(), olive::Rational(4));
	auto *clip_b = create_clip(project.get(), olive::Rational(4));
	create_track_with_clip(project.get(), clip_a);
	clip_b->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	create_track_with_clip(project.get(), clip_b);

	{
		olive::SpeedDurationDialog dialog({ reinterpret_cast<OakEngineBlock *>(clip_a), reinterpret_cast<OakEngineBlock *>(clip_b) },
										  olive::Rational(1, 24));
		// Speed is tristate, so accept() must compute each clip's speed
		// from its own length/speed ratio: speed = old_speed * old_len / new_len
		dialog.findChild<olive::RationalSlider *>()->set_value(
			olive::Rational(2));
		dialog.accept();
	}

	EXPECT_EQ(clip_a->length(), olive::Rational(2));
	EXPECT_EQ(clip_b->length(), olive::Rational(2));
	EXPECT_DOUBLE_EQ(clip_a->speed(), 2.0);
	EXPECT_DOUBLE_EQ(clip_b->speed(), 4.0);

	clear_undo_stack();
}

//
// keyframeproperties
//
TEST(DialogKeyframeProperties, SingleKeyAcceptWritesAllFields)
{
	ensure_app_singletons();
	auto project = create_project();

	// A 24 fps sequence gives the facade's keyframe timestamps a
	// frame-aligned timebase (the dialog moves the key to 1/2 s = 12
	// frames; without a sequence the facade's default timebase would not
	// land on it exactly).
	auto *seq = new olive::Sequence();
	seq->setParent(project.get());
	seq->set_video_params(olive::VideoParams(
		1920, 1080, olive::Rational(1, 24), olive::PixelFormat::f32,
		olive::VideoParams::k_internal_channel_count));

	auto *node = new olive::MathNode();
	node->setParent(project.get());
	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);
	key->setParent(node);

	// The dialog stores the keyframe vector by reference, so it must outlive it
	QVector<oak::Keyframe> keys = { oak::Keyframe(
		reinterpret_cast<OakEngineKeyframe *>(key)) };

	{
		olive::KeyframePropertiesDialog dialog(keys, olive::Rational(1, 24));

		auto *time_slider = dialog.findChild<olive::RationalSlider *>();
		auto *type_select = dialog.findChild<QComboBox *>();
		auto *bezier_group = dialog.findChild<QGroupBox *>();
		ASSERT_NE(time_slider, nullptr);
		ASSERT_NE(type_select, nullptr);
		ASSERT_NE(bezier_group, nullptr);

		// Initial state reflects the keyframe
		EXPECT_TRUE(time_slider->isEnabled());
		EXPECT_EQ(time_slider->get_value(), olive::Rational(0));
		ASSERT_EQ(type_select->count(), 3);
		EXPECT_EQ(type_select->currentData().toInt(), olive::NodeKeyframe::k_linear);
		EXPECT_FALSE(bezier_group->isEnabled());

		// Switching to Bezier enables the bezier handle editors. Item data
		// carries facade easing ordinals (linear=0, bezier=1, hold=2), not
		// the engine NodeKeyframe::Type values
		type_select->setCurrentIndex(2);
		ASSERT_EQ(type_select->currentData().toInt(),
				  olive::KeyframeTypes::k_facade_bezier);
		EXPECT_TRUE(bezier_group->isEnabled());

		time_slider->set_value(olive::Rational(1, 2));

		const QList<olive::FloatSlider *> sliders =
			dialog.findChildren<olive::FloatSlider *>();
		ASSERT_EQ(sliders.size(), 4);
		sliders.at(0)->set_value(0.1); // bezier in x
		sliders.at(1)->set_value(0.2); // bezier in y
		sliders.at(2)->set_value(0.3); // bezier out x
		sliders.at(3)->set_value(0.4); // bezier out y

		dialog.accept();
	}

	EXPECT_EQ(key->time(), olive::Rational(1, 2));
	EXPECT_EQ(key->type(), olive::NodeKeyframe::k_bezier);
	EXPECT_DOUBLE_EQ(key->bezier_control_in().x(), 0.1);
	EXPECT_DOUBLE_EQ(key->bezier_control_in().y(), 0.2);
	EXPECT_DOUBLE_EQ(key->bezier_control_out().x(), 0.3);
	EXPECT_DOUBLE_EQ(key->bezier_control_out().y(), 0.4);

	clear_undo_stack();
}

TEST(DialogKeyframeProperties, MixedTypesAddPlaceholderItem)
{
	ensure_app_singletons();
	auto project = create_project();

	auto *node = new olive::MathNode();
	node->setParent(project.get());
	auto *key_a = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										  olive::NodeKeyframe::k_linear, 0, -1,
										  olive::MathNode::k_param_a_in);
	auto *key_b = new olive::NodeKeyframe(olive::Rational(1), 2.0,
										  olive::NodeKeyframe::k_hold, 0, -1,
										  olive::MathNode::k_param_a_in);
	key_a->setParent(node);
	key_b->setParent(node);

	// The dialog stores the keyframe vector by reference, so it must outlive it
	QVector<oak::Keyframe> keys = {
		oak::Keyframe(reinterpret_cast<OakEngineKeyframe *>(key_a)),
		oak::Keyframe(reinterpret_cast<OakEngineKeyframe *>(key_b)),
	};

	{
		olive::KeyframePropertiesDialog dialog(keys, olive::Rational(1, 24));

		auto *type_select = dialog.findChild<QComboBox *>();
		// An "--" placeholder item with data -1 is prepended for mixed types
		ASSERT_EQ(type_select->count(), 4);
		EXPECT_EQ(type_select->itemData(0).toInt(), -1);
		EXPECT_EQ(type_select->currentIndex(), 0);

		dialog.accept();
	}

	// Accepting with the placeholder selected must not change key types
	EXPECT_EQ(key_a->type(), olive::NodeKeyframe::k_linear);
	EXPECT_EQ(key_b->type(), olive::NodeKeyframe::k_hold);

	clear_undo_stack();
}

TEST(DialogKeyframeProperties, KeysOnSameTrackDisableTimeEdit)
{
	ensure_app_singletons();
	auto project = create_project();

	auto *node = new olive::MathNode();
	node->setParent(project.get());
	auto *key_a = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										  olive::NodeKeyframe::k_linear, 0, -1,
										  olive::MathNode::k_param_a_in);
	auto *key_b = new olive::NodeKeyframe(olive::Rational(1), 2.0,
										  olive::NodeKeyframe::k_linear, 0, -1,
										  olive::MathNode::k_param_a_in);
	key_a->setParent(node);
	key_b->setParent(node);

	// The dialog stores the keyframe vector by reference, so it must outlive it
	QVector<oak::Keyframe> keys = {
		oak::Keyframe(reinterpret_cast<OakEngineKeyframe *>(key_a)),
		oak::Keyframe(reinterpret_cast<OakEngineKeyframe *>(key_b)),
	};

	olive::KeyframePropertiesDialog dialog(keys, olive::Rational(1, 24));

	// Moving two keys of the same track in time could reorder them, so the
	// time editor must be disabled
	EXPECT_FALSE(dialog.findChild<olive::RationalSlider *>()->isEnabled());
}

//
// markerproperties
//
TEST(DialogMarkerProperties, SingleMarkerAcceptWritesFields)
{
	ensure_app_singletons();

	olive::TimelineMarker marker(
		3,
		olive::TimeRange(olive::Rational(1), olive::Rational(2)),
		QStringLiteral("Marker A"));

	{
		olive::MarkerPropertiesDialog dialog({ reinterpret_cast<OakEngineMarker *>(&marker) },
											 olive::Rational(1, 24));

		auto *label_edit = dialog.findChild<QLineEdit *>();
		auto *color_menu = dialog.findChild<olive::ColorCodingComboBox *>();
		const QList<olive::RationalSlider *> sliders =
			dialog.findChildren<olive::RationalSlider *>();
		ASSERT_NE(label_edit, nullptr);
		ASSERT_NE(color_menu, nullptr);
		ASSERT_EQ(sliders.size(), 2);

		EXPECT_EQ(label_edit->text(), QStringLiteral("Marker A"));
		EXPECT_EQ(color_menu->get_selected_color(), 3);
		EXPECT_EQ(sliders.at(0)->get_value(), olive::Rational(1));
		EXPECT_EQ(sliders.at(1)->get_value(), olive::Rational(2));

		label_edit->setText(QStringLiteral("Renamed"));
		sliders.at(0)->set_value(olive::Rational(1, 2));
		sliders.at(1)->set_value(olive::Rational(3, 2));

		dialog.accept();
	}

	EXPECT_EQ(marker.name(), QStringLiteral("Renamed"));
	EXPECT_EQ(marker.time().in(), olive::Rational(1, 2));
	EXPECT_EQ(marker.time().out(), olive::Rational(3, 2));
	EXPECT_EQ(marker.color(), 3);

	clear_undo_stack();
}

TEST(DialogMarkerProperties, MultipleMarkersDisableTimeAndShowPlaceholder)
{
	ensure_app_singletons();

	olive::TimelineMarker marker_a(
		1,
		olive::TimeRange(olive::Rational(1), olive::Rational(2)),
		QStringLiteral("Alpha"));
	olive::TimelineMarker marker_b(
		2,
		olive::TimeRange(olive::Rational(5), olive::Rational(6)),
		QStringLiteral("Beta"));

	{
		olive::MarkerPropertiesDialog dialog({ reinterpret_cast<OakEngineMarker *>(&marker_a), reinterpret_cast<OakEngineMarker *>(&marker_b) },
											 olive::Rational(1, 24));

		auto *label_edit = dialog.findChild<QLineEdit *>();
		auto *color_menu = dialog.findChild<olive::ColorCodingComboBox *>();
		const QList<olive::RationalSlider *> sliders =
			dialog.findChildren<olive::RationalSlider *>();

		// Time cannot be edited for multiple markers
		EXPECT_FALSE(sliders.at(0)->isEnabled());
		EXPECT_TRUE(sliders.at(0)->is_tristate());
		EXPECT_FALSE(sliders.at(1)->isEnabled());
		EXPECT_TRUE(sliders.at(1)->is_tristate());

		// Differing names show a placeholder instead of text
		EXPECT_TRUE(label_edit->text().isEmpty());
		EXPECT_FALSE(label_edit->placeholderText().isEmpty());

		// Differing colors are represented by -1
		EXPECT_EQ(color_menu->get_selected_color(), -1);

		dialog.accept();
	}

	// Nothing should have been written back
	EXPECT_EQ(marker_a.name(), QStringLiteral("Alpha"));
	EXPECT_EQ(marker_b.name(), QStringLiteral("Beta"));
	EXPECT_EQ(marker_a.color(), 1);
	EXPECT_EQ(marker_b.color(), 2);

	clear_undo_stack();
}

//
// sequence
//
TEST(DialogSequencePreset, SaveLoadRoundTrip)
{
	olive::SequencePreset preset(QStringLiteral("Test Preset"), 1920, 1080,
								 olive::Rational(24, 1), olive::Rational(1, 1),
								 olive::VideoParams::k_interlaced_top_first, 48000,
								 olive::core::k_channel_layout_stereo, 2,
								 olive::PixelFormat::f16, true);

	QByteArray xml;
	QBuffer buffer(&xml);
	buffer.open(QIODevice::WriteOnly);
	QXmlStreamWriter writer(&buffer);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("preset"));
	preset.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();
	buffer.close();

	olive::SequencePreset loaded;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name().toString(), QStringLiteral("preset"));
	loaded.load(&reader);

	EXPECT_EQ(loaded.get_name(), QStringLiteral("Test Preset"));
	EXPECT_EQ(loaded.width(), 1920);
	EXPECT_EQ(loaded.height(), 1080);
	EXPECT_EQ(loaded.frame_rate(), olive::Rational(24, 1));
	EXPECT_EQ(loaded.pixel_aspect(), olive::Rational(1, 1));
	EXPECT_EQ(loaded.interlacing(), olive::VideoParams::k_interlaced_top_first);
	EXPECT_EQ(loaded.sample_rate(), 48000);
	EXPECT_EQ(loaded.channel_layout(), olive::core::k_channel_layout_stereo);
	EXPECT_EQ(loaded.preview_divider(), 2);
	EXPECT_EQ(loaded.preview_format(), olive::PixelFormat::f16);
	EXPECT_TRUE(loaded.preview_autocache());
}

TEST(DialogSequencePreset, LoadsLegacyInterlacingElement)
{
	// Older builds wrote the interlacing element as "interlacing_"; such
	// preset files must still load
	QByteArray xml = R"(<preset>
<name>Legacy</name>
<width>1280</width>
<height>720</height>
<framerate>24/1</framerate>
<pixelaspect>1/1</pixelaspect>
<interlacing_>2</interlacing_>
<samplerate>44100</samplerate>
<chlayout>3</chlayout>
<divider>1</divider>
<format>4</format>
<autocache>0</autocache>
</preset>)";

	olive::SequencePreset loaded;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name().toString(), QStringLiteral("preset"));
	loaded.load(&reader);

	EXPECT_EQ(loaded.get_name(), QStringLiteral("Legacy"));
	EXPECT_EQ(loaded.width(), 1280);
	EXPECT_EQ(loaded.interlacing(),
			  static_cast<olive::VideoParams::Interlacing>(2));
}

TEST(DialogSequenceParameterTab, ReflectsSequenceParameters)
{
	auto project = create_project();

	auto *sequence = new olive::Sequence();
	sequence->setParent(project.get());
	sequence->set_video_params(olive::VideoParams(
		1920, 1080, olive::Rational(1001, 30000), olive::PixelFormat::f32,
		olive::VideoParams::k_internal_channel_count, olive::Rational(1, 1),
		olive::VideoParams::k_interlace_none, 2));
	sequence->set_audio_params(olive::AudioParams(
		48000, olive::core::k_channel_layout_stereo,
		olive::Sequence::k_default_sample_format));

	olive::SequenceDialogParameterTab tab(reinterpret_cast<OakEngineNode *>(sequence));

	EXPECT_EQ(tab.get_selected_video_width(), 1920);
	EXPECT_EQ(tab.get_selected_video_height(), 1080);
	EXPECT_EQ(tab.get_selected_video_frame_rate(), olive::Rational(30000, 1001));
	EXPECT_EQ(tab.get_selected_video_pixel_aspect(), olive::Rational(1, 1));
	EXPECT_EQ(tab.get_selected_video_interlacing_mode(),
			  olive::VideoParams::k_interlace_none);
	EXPECT_EQ(tab.get_selected_preview_resolution(), 2);
	EXPECT_EQ(tab.get_selected_preview_format(), olive::PixelFormat::f32);
	EXPECT_EQ(tab.get_selected_audio_sample_rate(), 48000);
	EXPECT_EQ(tab.get_selected_audio_channel_layout(),
			  olive::core::k_channel_layout_stereo);
}

TEST(DialogSequenceParameterTab, PresetChangedAppliesValues)
{
	auto project = create_project();

	auto *sequence = new olive::Sequence();
	sequence->setParent(project.get());
	sequence->set_video_params(olive::VideoParams(
		1920, 1080, olive::Rational(1, 24), olive::PixelFormat::f32,
		olive::VideoParams::k_internal_channel_count, olive::Rational(1, 1),
		olive::VideoParams::k_interlace_none, 1));
	sequence->set_audio_params(olive::AudioParams(
		48000, olive::core::k_channel_layout_stereo,
		olive::Sequence::k_default_sample_format));

	olive::SequenceDialogParameterTab tab(reinterpret_cast<OakEngineNode *>(sequence));

	tab.preset_changed(olive::SequencePreset(
		QStringLiteral("Preset"), 1280, 720, olive::Rational(24, 1),
		olive::Rational(1, 1), olive::VideoParams::k_interlaced_top_first, 44100,
		olive::core::k_channel_layout_stereo, 4, olive::PixelFormat::f16, false));

	EXPECT_EQ(tab.get_selected_video_width(), 1280);
	EXPECT_EQ(tab.get_selected_video_height(), 720);
	EXPECT_EQ(tab.get_selected_video_frame_rate(), olive::Rational(24, 1));
	EXPECT_EQ(tab.get_selected_video_interlacing_mode(),
			  olive::VideoParams::k_interlaced_top_first);
	EXPECT_EQ(tab.get_selected_audio_sample_rate(), 44100);
	EXPECT_EQ(tab.get_selected_preview_resolution(), 4);
	EXPECT_EQ(tab.get_selected_preview_format(), olive::PixelFormat::f16);
}

TEST(DialogSequenceDialog, AcceptNonUndoableAppliesParameters)
{
	StandardPathsTestModeGuard test_mode;
	ensure_app_singletons();
	auto project = create_project();

	auto *sequence = new olive::Sequence();
	sequence->setParent(project.get());
	sequence->set_label(QStringLiteral("Seq A"));
	sequence->set_video_params(olive::VideoParams(
		1920, 1080, olive::Rational(1, 24), olive::PixelFormat::f32,
		olive::VideoParams::k_internal_channel_count, olive::Rational(1, 1),
		olive::VideoParams::k_interlace_none, 1));
	sequence->set_audio_params(olive::AudioParams(
		48000, olive::core::k_channel_layout_stereo,
		olive::Sequence::k_default_sample_format));

	{
		olive::SequenceDialog dialog(reinterpret_cast<OakEngineNode *>(sequence), olive::SequenceDialog::k_existing);
		dialog.set_undoable(false);

		auto *tab = dialog.findChild<olive::SequenceDialogParameterTab *>();
		ASSERT_NE(tab, nullptr);
		tab->preset_changed(olive::SequencePreset(
			QStringLiteral("Preset"), 1280, 720, olive::Rational(24, 1),
			olive::Rational(1, 1), olive::VideoParams::k_interlaced_top_first,
			44100, olive::core::k_channel_layout_stereo, 4,
			olive::PixelFormat::f32, false));

		auto *name_field = dialog.findChild<QLineEdit *>();
		ASSERT_NE(name_field, nullptr);
		name_field->setText(QStringLiteral("Seq B"));

		dialog.accept();
		EXPECT_EQ(dialog.result(), QDialog::Accepted);
	}

	EXPECT_EQ(sequence->get_label(), QStringLiteral("Seq B"));
	EXPECT_EQ(sequence->get_video_params().width(), 1280);
	EXPECT_EQ(sequence->get_video_params().height(), 720);
	EXPECT_EQ(sequence->get_video_params().frame_rate(), olive::Rational(24, 1));
	EXPECT_EQ(sequence->get_video_params().interlacing(),
			  olive::VideoParams::k_interlaced_top_first);
	EXPECT_EQ(sequence->get_video_params().divider(), 4);
	EXPECT_EQ(sequence->get_audio_params().sample_rate(), 44100);
}

TEST(DialogSequenceDialog, AcceptUndoablePushesCommand)
{
	StandardPathsTestModeGuard test_mode;
	ensure_app_singletons();
	auto project = create_project();

	auto *sequence = new olive::Sequence();
	sequence->setParent(project.get());
	sequence->set_label(QStringLiteral("Seq A"));
	sequence->set_video_params(olive::VideoParams(
		1920, 1080, olive::Rational(1, 24), olive::PixelFormat::f32,
		olive::VideoParams::k_internal_channel_count, olive::Rational(1, 1),
		olive::VideoParams::k_interlace_none, 1));
	sequence->set_audio_params(olive::AudioParams(
		48000, olive::core::k_channel_layout_stereo,
		olive::Sequence::k_default_sample_format));

	{
		olive::SequenceDialog dialog(reinterpret_cast<OakEngineNode *>(sequence), olive::SequenceDialog::k_existing);

		auto *tab = dialog.findChild<olive::SequenceDialogParameterTab *>();
		ASSERT_NE(tab, nullptr);
		tab->preset_changed(olive::SequencePreset(
			QStringLiteral("Preset"), 640, 360, olive::Rational(24, 1),
			olive::Rational(1, 1), olive::VideoParams::k_interlace_none, 48000,
			olive::core::k_channel_layout_stereo, 1, olive::PixelFormat::f32,
			false));

		dialog.accept();
		EXPECT_EQ(dialog.result(), QDialog::Accepted);
	}

	EXPECT_EQ(sequence->get_video_params().width(), 640);
	EXPECT_EQ(sequence->get_video_params().height(), 360);

	clear_undo_stack();
}

TEST(DialogSequenceDialog, PresetTabListsDefaultPresets)
{
	StandardPathsTestModeGuard test_mode;
	ensure_app_singletons();
	auto project = create_project();

	auto *sequence = new olive::Sequence();
	sequence->setParent(project.get());

	olive::SequenceDialog dialog(reinterpret_cast<OakEngineNode *>(sequence), olive::SequenceDialog::k_existing);

	auto *tree = dialog.findChild<QTreeWidget *>();
	ASSERT_NE(tree, nullptr);
	// "My Presets" plus the standard preset folders
	EXPECT_GE(tree->topLevelItemCount(), 4);
}

//
// footageproperties
//
TEST(DialogFootageProperties, AcceptRenamesAndSetsSourceStartTime)
{
	ensure_app_singletons();
	auto project = create_project();

	auto *footage = new olive::Footage();
	footage->setParent(project.get());
	footage->set_label(QStringLiteral("Clip A"));
	footage->set_filename(QStringLiteral("/tmp/oak-nonexistent.mp4"));

	{
		olive::FootagePropertiesDialog dialog(nullptr, reinterpret_cast<OakEngineNode *>(footage));

		auto *name_field = dialog.findChild<QLineEdit *>();
		auto *start_enable = dialog.findChild<QCheckBox *>();
		auto *start_spin = dialog.findChild<QDoubleSpinBox *>();
		ASSERT_NE(name_field, nullptr);
		ASSERT_NE(start_enable, nullptr);
		ASSERT_NE(start_spin, nullptr);

		EXPECT_EQ(name_field->text(), QStringLiteral("Clip A"));
		EXPECT_FALSE(start_enable->isChecked());
		EXPECT_FALSE(start_spin->isEnabled());

		name_field->setText(QStringLiteral("Clip B"));
		start_enable->setChecked(true);
		EXPECT_TRUE(start_spin->isEnabled());
		start_spin->setValue(12.5);

		// accept() is a private slot, invoke it through the meta-object
		QMetaObject::invokeMethod(&dialog, "accept");
	}

	EXPECT_EQ(footage->get_label(), QStringLiteral("Clip B"));
	EXPECT_TRUE(footage->has_source_start_time());
	EXPECT_EQ(footage->source_start_time(), olive::Rational(25, 2));
	EXPECT_EQ(footage->source_start_time_source(), QStringLiteral("manual"));

	{
		// Unchecking the box must clear the source start time again
		olive::FootagePropertiesDialog dialog(nullptr, reinterpret_cast<OakEngineNode *>(footage));

		auto *start_enable = dialog.findChild<QCheckBox *>();
		ASSERT_NE(start_enable, nullptr);
		EXPECT_TRUE(start_enable->isChecked());
		start_enable->setChecked(false);

		// accept() is a private slot, invoke it through the meta-object
		QMetaObject::invokeMethod(&dialog, "accept");
	}

	EXPECT_FALSE(footage->has_source_start_time());

	clear_undo_stack();
}

//
// footagerelink
//
TEST(DialogFootageRelink, TableListsFootageAndFilenames)
{
	auto project = create_project();

	auto *footage_a = new olive::Footage();
	footage_a->setParent(project.get());
	footage_a->set_label(QStringLiteral("Footage A"));
	footage_a->set_filename(QStringLiteral("/old/path/a.mp4"));

	auto *footage_b = new olive::Footage();
	footage_b->setParent(project.get());
	footage_b->set_label(QStringLiteral("Footage B"));
	footage_b->set_filename(QStringLiteral("/old/path/b.mp4"));

	olive::FootageRelinkDialog dialog({ reinterpret_cast<OakEngineNode *>(footage_a), reinterpret_cast<OakEngineNode *>(footage_b) });

	auto *table = dialog.findChild<QTreeWidget *>();
	ASSERT_NE(table, nullptr);
	ASSERT_EQ(table->topLevelItemCount(), 2);
	EXPECT_EQ(table->topLevelItem(0)->text(0), QStringLiteral("Footage A"));
	EXPECT_EQ(table->topLevelItem(0)->text(1),
			  QStringLiteral("/old/path/a.mp4"));
	EXPECT_EQ(table->topLevelItem(1)->text(0), QStringLiteral("Footage B"));
	EXPECT_EQ(table->topLevelItem(1)->text(1),
			  QStringLiteral("/old/path/b.mp4"));
}

//
// projectproperties
//
TEST(DialogProjectProperties, OcioValidationTogglesOnInvalidFilename)
{
	ensure_app_singletons();
	auto project = create_project();

	olive::ProjectPropertiesDialog dialog(reinterpret_cast<OakEngineProject *>(project.get()), nullptr);

	EXPECT_TRUE(dialog.windowTitle().contains(project->name()));

	// The first QLineEdit in the dialog is the OCIO config filename
	auto *ocio_edit = dialog.findChild<QLineEdit *>();
	ASSERT_NE(ocio_edit, nullptr);

	// A bad config path flags the line edit as invalid (red text). Use a
	// nonexistent file inside a temp dir: a fixed absolute path is not
	// guaranteed to stay nonexistent on every platform (e.g. an earlier test
	// may have created parts of it on a writable drive).
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	ocio_edit->setText(dir.filePath(QStringLiteral("nonexistent.ocio")));
	EXPECT_TRUE(ocio_edit->styleSheet().contains(QStringLiteral("red")));

	// Restoring an empty (default) filename clears the error again
	ocio_edit->setText(QString());
	EXPECT_TRUE(ocio_edit->styleSheet().isEmpty());

	// The default config provides selectable input color spaces
	bool found_populated_combo = false;
	foreach (QComboBox *combo, dialog.findChildren<QComboBox *>()) {
		if (combo->count() > 2) {
			found_populated_combo = true;
			break;
		}
	}
	EXPECT_TRUE(found_populated_combo);
}

TEST(DialogProjectProperties, AcceptWithDefaultsClosesDialog)
{
	ensure_app_singletons();
	auto project = create_project();

	olive::ProjectPropertiesDialog dialog(reinterpret_cast<OakEngineProject *>(project.get()), nullptr);
	dialog.accept();

	EXPECT_EQ(dialog.result(), QDialog::Accepted);
}
