#include <gtest/gtest.h>

#include <memory>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QImage>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "dialog/about/scrollinglabel.h"
#include "dialog/export/codec/av1section.h"
#include "dialog/export/codec/cineformsection.h"
#include "dialog/export/codec/codecsection.h"
#include "dialog/export/codec/codecstack.h"
#include "dialog/footageproperties/streamproperties/audiostreamproperties.h"
#include "dialog/footageproperties/streamproperties/streamproperties.h"
#include "dialog/footageproperties/streamproperties/videostreamproperties.h"
#include "dialog/sequence/presetmanager.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "oakengine/encoding.h"
#include "oakengine/footage.h"
#include "oakutil/qtutils.h"
#include "render/videoparams.h"
#include "widget/slider/integerslider.h"
#include "widget/standardcombos/interlacedcombobox.h"
#include "widget/standardcombos/pixelaspectratiocombobox.h"

// dialog/about/patreon.h defines a global `QStringList patrons` (without
// extern); about.cpp already pulls that definition into the test binary, so
// including the header here would create a duplicate symbol. Reference the
// existing definition with a matching extern declaration instead.
extern QStringList patrons;

namespace
{

// Redirects QStandardPaths (used for the preset file location) to a
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

// Concrete Preset with a name and an integer so the PresetManager load/save
// cycle has something observable to round-trip
class NameValuePreset : public olive::Preset {
public:
	int value_ = 0;

	virtual void load(QXmlStreamReader *reader) override
	{
		while (olive::xml_read_next_start_element(reader)) {
			if (reader->name() == QStringLiteral("name")) {
				set_name(reader->readElementText());
			} else if (reader->name() == QStringLiteral("value")) {
				value_ = reader->readElementText().toInt();
			} else {
				reader->skipCurrentElement();
			}
		}
	}

	virtual void save(QXmlStreamWriter *writer) const override
	{
		writer->writeTextElement(QStringLiteral("name"), get_name());
		writer->writeTextElement(QStringLiteral("value"),
								 QString::number(value_));
	}
};

// Footage subclass that exposes the protected stream mutator of ViewerOutput
// so tests can populate streams without probing real media
class TestableFootage : public olive::Footage {
public:
	using olive::ViewerOutput::add_stream;
};

// A footage node with two video streams; the second carries the given
// (possibly empty) explicit colorspace override
TestableFootage *make_two_stream_footage(olive::Project *project,
										 const QString &explicit_colorspace)
{
	auto *footage = new TestableFootage();
	footage->setParent(project);

	olive::VideoParams v0(1920, 1080, olive::Rational(1, 24),
						  olive::core::PixelFormat::u8, 4);
	v0.set_stream_index(0);
	v0.set_duration(48);
	footage->add_stream(olive::Track::k_video, QVariant::fromValue(v0));

	olive::VideoParams v1(1280, 720, olive::Rational(1, 24),
						  olive::core::PixelFormat::u8, 4);
	v1.set_stream_index(1);
	v1.set_duration(48);
	v1.set_colorspace(explicit_colorspace);
	footage->add_stream(olive::Track::k_video, QVariant::fromValue(v1));

	return footage;
}

// The plain (non-combo-class) QComboBoxes of a VideoStreamProperties, in
// layout order: color space first, then color range. The widget also holds a
// PixelAspectRatioComboBox and an InterlacedComboBox, so filter those out.
QList<QComboBox *> plain_combos(QWidget *w)
{
	QList<QComboBox *> out;
	foreach (QComboBox *c, w->findChildren<QComboBox *>()) {
		if (!qobject_cast<olive::PixelAspectRatioComboBox *>(c) &&
			!qobject_cast<olive::InterlacedComboBox *>(c)) {
			out.append(c);
		}
	}
	return out;
}

// Renders the widget and counts pixels drawn in its palette text color.
// QWidget::render() fills an opaque window-color background, so alpha-based
// checks are useless; the scrolling text is painted in palette().text().
int count_text_pixels(QWidget *w)
{
	QImage img(w->size(), QImage::Format_ARGB32);
	img.fill(Qt::transparent);
	w->render(&img);

	const QColor text = w->palette().text().color();

	int count = 0;
	for (int y = 0; y < img.height(); y++) {
		for (int x = 0; x < img.width(); x++) {
			const QColor px = img.pixelColor(x, y);
			if (qAbs(px.red() - text.red()) < 16 &&
				qAbs(px.green() - text.green()) < 16 &&
				qAbs(px.blue() - text.blue()) < 16) {
				count++;
			}
		}
	}
	return count;
}

} // namespace

//
// about: patreon
//
TEST(DialogAboutPatreon, PatronsListIsEmptyInSourceTree)
{
	// The committed patreon.h is generated with no entries; patreon.py fills
	// the list in when packaging a release
	EXPECT_TRUE(patrons.isEmpty());
}

//
// about: scrollinglabel
//
TEST(DialogScrollingLabel, SetTextSizesMinimumToContent)
{
	olive::ScrollingLabel label;
	EXPECT_EQ(label.minimumSize(), QSize(0, 0));

	const QString long_line =
		QStringLiteral("A considerably longer line of text");
	label.set_text({ QStringLiteral("Hi"), long_line });

	QFontMetrics fm = label.fontMetrics();

	// Minimum height is always ten lines, minimum width the widest line
	EXPECT_EQ(label.minimumHeight(), fm.height() * 10);
	EXPECT_EQ(label.minimumWidth(),
			  olive::QtUtils::q_font_metrics_width(fm, long_line));

	// A narrower text shrinks the minimum width again
	label.set_text({ QStringLiteral("Hi") });
	EXPECT_EQ(label.minimumWidth(),
			  olive::QtUtils::q_font_metrics_width(fm, QStringLiteral("Hi")));
	EXPECT_EQ(label.minimumHeight(), fm.height() * 10);

	// The text-list constructor applies the same sizing
	olive::ScrollingLabel label2({ QStringLiteral("Hi") });
	EXPECT_EQ(label2.minimumWidth(),
			  olive::QtUtils::q_font_metrics_width(fm, QStringLiteral("Hi")));
}

TEST(DialogScrollingLabel, EmptyTextKeepsOnlyLineHeightMinimum)
{
	olive::ScrollingLabel label;
	label.set_text({});

	EXPECT_EQ(label.minimumWidth(), 0);
	EXPECT_EQ(label.minimumHeight(), label.fontMetrics().height() * 10);
}

TEST(DialogScrollingLabel, AnimationScrollsTextIntoView)
{
	olive::ScrollingLabel label(
		{ QStringLiteral("Hello"), QStringLiteral("World") });
	label.resize(120, 200);

	// At offset zero every line sits below the widget: nothing is painted
	EXPECT_EQ(count_text_pixels(&label), 0);

	// animation_update() is a private slot; drive it through the meta-object.
	// Half the widget height puts the first line near the vertical middle.
	for (int i = 0; i < 100; i++) {
		QMetaObject::invokeMethod(&label, "animation_update");
	}
	EXPECT_GT(count_text_pixels(&label), 0);
}

TEST(DialogScrollingLabel, AnimationWrapsAfterFullCycle)
{
	olive::ScrollingLabel label(
		{ QStringLiteral("Hello"), QStringLiteral("World") });
	label.resize(120, 200);

	// animate_ resets to 0 once it reaches height + lines * line_height
	const int cycle = 200 + 2 * label.fontMetrics().height();
	for (int i = 0; i < cycle; i++) {
		QMetaObject::invokeMethod(&label, "animation_update");
	}

	// Back at offset zero the text has scrolled out of view again
	EXPECT_EQ(count_text_pixels(&label), 0);

	// start/stop only toggle the internal timer
	label.start_animating();
	label.stop_animating();
}

//
// sequence: presetmanager
//
TEST(DialogPresetManager, StartsEmptyWhenPresetFileMissing)
{
	StandardPathsTestModeGuard test_mode;

	const QString preset_name =
		QStringLiteral("oak-gtest-presetmanager-missing");
	const QString path =
		QDir(olive::FileFunctions::get_configuration_location())
			.filePath(preset_name);
	QFile::remove(path);

	{
		olive::PresetManager<NameValuePreset> mgr(nullptr, preset_name);
		EXPECT_EQ(mgr.get_number_of_presets(), 0);
		EXPECT_TRUE(mgr.get_preset_data().isEmpty());
		EXPECT_TRUE(mgr.get_custom_preset_filename().endsWith(preset_name));
	}

	// The destructor wrote an empty preset file; clean it up
	QFile::remove(path);
}

TEST(DialogPresetManager, LoadsPresetsFromXmlFile)
{
	StandardPathsTestModeGuard test_mode;

	const QString preset_name =
		QStringLiteral("oak-gtest-presetmanager-load");
	const QString path =
		QDir(olive::FileFunctions::get_configuration_location())
			.filePath(preset_name);
	QFile::remove(path);

	{
		QFile f(path);
		ASSERT_TRUE(f.open(QFile::WriteOnly));
		f.write("<?xml version=\"1.0\"?>\n"
				"<presets>\n"
				"  <preset><name>Alpha</name><value>7</value></preset>\n"
				"  <preset><name>Beta</name><value>8</value></preset>\n"
				"</presets>\n");
		f.close();
	}

	{
		olive::PresetManager<NameValuePreset> mgr(nullptr, preset_name);

		ASSERT_EQ(mgr.get_number_of_presets(), 2);
		EXPECT_EQ(mgr.get_preset(0)->get_name(), QStringLiteral("Alpha"));
		EXPECT_EQ(mgr.get_preset(1)->get_name(), QStringLiteral("Beta"));
		EXPECT_EQ(
			std::static_pointer_cast<NameValuePreset>(mgr.get_preset(0))->value_,
			7);
		EXPECT_EQ(
			std::static_pointer_cast<NameValuePreset>(mgr.get_preset(1))->value_,
			8);
		EXPECT_EQ(mgr.get_preset_data().size(), 2);

		mgr.delete_preset(0);
		ASSERT_EQ(mgr.get_number_of_presets(), 1);
		EXPECT_EQ(mgr.get_preset(0)->get_name(), QStringLiteral("Beta"));

		// Change the surviving preset so the destructor's save has something
		// new to write
		std::static_pointer_cast<NameValuePreset>(mgr.get_preset(0))->value_ =
			42;
	}

	// The destructor persisted the current preset list to disk
	{
		olive::PresetManager<NameValuePreset> mgr(nullptr, preset_name);
		ASSERT_EQ(mgr.get_number_of_presets(), 1);
		EXPECT_EQ(mgr.get_preset(0)->get_name(), QStringLiteral("Beta"));
		EXPECT_EQ(
			std::static_pointer_cast<NameValuePreset>(mgr.get_preset(0))->value_,
			42);
	}

	QFile::remove(path);
}

//
// export: codecsection (base class)
//
TEST(DialogCodecSection, BaseClassOptsAreNoOps)
{
	olive::CodecSection section;

	OakEngineEncodingParams *params = oakengine_encoding_params_create();
	ASSERT_NE(params, nullptr);

	section.add_opts(params);

	// A base CodecSection writes no encoder options
	char buf[64];
	EXPECT_EQ(oakengine_encoding_params_video_option(params, "qp", buf,
													 static_cast<int>(sizeof(buf))),
			  OAKENGINE_E_NOT_FOUND);

	// set_opts must accept anything without touching the widget
	section.set_opts(params);

	oakengine_encoding_params_destroy(params);
}

//
// export: codecstack
//
TEST(DialogCodecStack, CurrentWidgetExpandsOthersAreIgnored)
{
	olive::CodecStack stack;
	QWidget a;
	QWidget b;

	stack.addWidget(&a);
	stack.addWidget(&b);

	ASSERT_EQ(stack.count(), 2);
	ASSERT_EQ(stack.currentIndex(), 0);

	// addWidget() reapplies the policies for the current index
	EXPECT_EQ(a.sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
	EXPECT_EQ(a.sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
	EXPECT_EQ(b.sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
	EXPECT_EQ(b.sizePolicy().verticalPolicy(), QSizePolicy::Ignored);

	// Switching pages swaps the policies
	stack.setCurrentIndex(1);
	EXPECT_EQ(a.sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
	EXPECT_EQ(b.sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
	EXPECT_EQ(b.sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
}

//
// export: av1section
//
TEST(DialogAV1CRFSection, DefaultsAndSliderRange)
{
	// Copy to a local first: EXPECT_EQ binds by reference, which would
	// odr-use the in-class-initialized static constant and fail to link
	const int default_crf = olive::AV1CRFSection::k_default_a_v1_crf;
	EXPECT_EQ(default_crf, 30);

	olive::AV1CRFSection section(olive::AV1CRFSection::k_default_a_v1_crf);
	EXPECT_EQ(section.get_value(), 30);

	QSlider *slider = section.findChild<QSlider *>();
	ASSERT_NE(slider, nullptr);
	EXPECT_EQ(slider->minimum(), 0);
	EXPECT_EQ(slider->maximum(), 63);

	// The slider is the value source, clamped to 0-63
	slider->setValue(45);
	EXPECT_EQ(section.get_value(), 45);

	slider->setValue(99);
	EXPECT_EQ(section.get_value(), 63);
}

TEST(DialogAV1CRFSection, SliderSyncsIntoIntegerInput)
{
	olive::AV1CRFSection section(30);

	QSlider *slider = section.findChild<QSlider *>();
	olive::IntegerSlider *input = section.findChild<olive::IntegerSlider *>();
	ASSERT_NE(slider, nullptr);
	ASSERT_NE(input, nullptr);
	EXPECT_EQ(input->get_value(), 30);

	// QSlider::valueChanged is wired to IntegerSlider::set_value
	slider->setValue(20);
	EXPECT_EQ(input->get_value(), 20);
}

TEST(DialogAV1Section, DefaultsAndAddOpts)
{
	olive::AV1Section section;

	// First combo is the preset list (0-13, default 8), second the
	// compression method list (Constant Rate Factor only)
	const auto combos = section.findChildren<QComboBox *>();
	ASSERT_EQ(combos.size(), 2);
	EXPECT_EQ(combos.at(0)->count(), 14);
	EXPECT_EQ(combos.at(0)->currentIndex(), 8);
	EXPECT_EQ(combos.at(1)->count(), 1);

	OakEngineEncodingParams *params = oakengine_encoding_params_create();
	ASSERT_NE(params, nullptr);

	char buf[64];

	// Default construction uses the default CRF
	section.add_opts(params);
	ASSERT_GT(oakengine_encoding_params_video_option(
				  params, "qp", buf, static_cast<int>(sizeof(buf))),
			  0);
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("30"));
	ASSERT_GT(oakengine_encoding_params_video_option(
				  params, "preset", buf, static_cast<int>(sizeof(buf))),
			  0);
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("8"));

	// Changed widget state is reflected in the written options
	combos.at(0)->setCurrentIndex(3);
	QSlider *slider = section.findChild<QSlider *>();
	ASSERT_NE(slider, nullptr);
	slider->setValue(20);

	section.add_opts(params);
	ASSERT_GT(oakengine_encoding_params_video_option(
				  params, "qp", buf, static_cast<int>(sizeof(buf))),
			  0);
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("20"));
	ASSERT_GT(oakengine_encoding_params_video_option(
				  params, "preset", buf, static_cast<int>(sizeof(buf))),
			  0);
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("3"));

	oakengine_encoding_params_destroy(params);
}

//
// export: cineformsection
//
TEST(DialogCineformSection, DefaultsToMediumQuality)
{
	olive::CineformSection section;

	QComboBox *quality = section.findChild<QComboBox *>();
	ASSERT_NE(quality, nullptr);

	// 13 FFmpeg quality levels (film3+ .. low), defaulting to "medium"
	EXPECT_EQ(quality->count(), 13);
	EXPECT_EQ(quality->currentIndex(), 10);
}

TEST(DialogCineformSection, OptsRoundTripThroughParams)
{
	olive::CineformSection section;
	QComboBox *quality = section.findChild<QComboBox *>();
	ASSERT_NE(quality, nullptr);

	OakEngineEncodingParams *params = oakengine_encoding_params_create();
	ASSERT_NE(params, nullptr);

	char buf[64];

	section.add_opts(params);
	ASSERT_GT(oakengine_encoding_params_video_option(
				  params, "quality", buf, static_cast<int>(sizeof(buf))),
			  0);
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("10"));

	quality->setCurrentIndex(5);
	section.add_opts(params);
	ASSERT_GT(oakengine_encoding_params_video_option(
				  params, "quality", buf, static_cast<int>(sizeof(buf))),
			  0);
	EXPECT_EQ(QString::fromUtf8(buf), QStringLiteral("5"));

	// set_opts applies a stored value back to the combo
	oakengine_encoding_params_set_video_option(params, "quality", "2");
	section.set_opts(params);
	EXPECT_EQ(quality->currentIndex(), 2);

	// A params handle without the key leaves the combo untouched
	OakEngineEncodingParams *empty = oakengine_encoding_params_create();
	ASSERT_NE(empty, nullptr);
	section.set_opts(empty);
	EXPECT_EQ(quality->currentIndex(), 2);

	oakengine_encoding_params_destroy(empty);
	oakengine_encoding_params_destroy(params);
}

//
// footageproperties: streamproperties (base class)
//
TEST(DialogStreamProperties, BaseClassIsANoOpStub)
{
	olive::StreamProperties props;

	EXPECT_TRUE(props.sanity_check());

	// accept() takes an optional parent pointer and does nothing
	props.accept(nullptr);
}

//
// footageproperties: audiostreamproperties
//
TEST(DialogAudioStreamProperties, ConstructsAroundFootageStream)
{
	olive::Project project;
	TestableFootage *footage = make_two_stream_footage(&project, QString());

	olive::AudioStreamProperties props(
		reinterpret_cast<OakEngineNode *>(footage), 0);

	// The class is currently a stub: no UI, default sanity check, no-op accept
	EXPECT_EQ(props.findChildren<QWidget *>().size(), 0);
	EXPECT_TRUE(props.sanity_check());
	props.accept(nullptr);
}

//
// footageproperties: videostreamproperties
//
TEST(DialogVideoStreamProperties, ReflectsStreamDefaults)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	TestableFootage *footage = make_two_stream_footage(&project, QString());

	olive::VideoStreamProperties props(
		reinterpret_cast<OakEngineNode *>(footage), 0);

	auto *interlace = props.findChild<olive::InterlacedComboBox *>();
	ASSERT_NE(interlace, nullptr);
	EXPECT_EQ(interlace->get_interlace_mode(),
			  olive::VideoParams::k_interlace_none);

	auto *par = props.findChild<olive::PixelAspectRatioComboBox *>();
	ASSERT_NE(par, nullptr);
	EXPECT_EQ(par->get_pixel_aspect_ratio(), olive::Rational(1, 1));

	const auto combos = plain_combos(&props);
	ASSERT_EQ(combos.size(), 2);

	// Color space: "Default (...)" entry followed by every config colorspace
	QComboBox *colorspace = combos.at(0);
	EXPECT_TRUE(colorspace->itemText(0).startsWith(QStringLiteral("Default (")));
	EXPECT_EQ(colorspace->count(),
			  1 + project.color_manager()->list_available_colorspaces().size());
	// No override on the stream: the default entry stays selected
	EXPECT_EQ(colorspace->currentIndex(), 0);

	// Color range: limited/full, matching the stream's limited default
	QComboBox *range = combos.at(1);
	ASSERT_EQ(range->count(), 2);
	EXPECT_EQ(range->itemData(0).toInt(),
			  olive::VideoParams::k_color_range_limited);
	EXPECT_EQ(range->itemData(1).toInt(), olive::VideoParams::k_color_range_full);
	EXPECT_EQ(range->currentIndex(), olive::VideoParams::k_color_range_limited);

	// Regular video is not an image sequence: no image sequence group box
	EXPECT_EQ(props.findChild<QGroupBox *>(), nullptr);

	// The premultiplied-alpha checkbox only exists for 4-channel processing
	QCheckBox *premult = props.findChild<QCheckBox *>();
	if (olive::VideoParams::k_internal_channel_count == 4) {
		ASSERT_NE(premult, nullptr);
		EXPECT_FALSE(premult->isChecked());
	} else {
		EXPECT_EQ(premult, nullptr);
	}

	// Not an image sequence, so sanity_check() passes without prompting
	EXPECT_TRUE(props.sanity_check());
}

TEST(DialogVideoStreamProperties, ReflectsExplicitColorspaceOverride)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	const QStringList spaces =
		project.color_manager()->list_available_colorspaces();
	ASSERT_FALSE(spaces.isEmpty());

	TestableFootage *footage =
		make_two_stream_footage(&project, spaces.first());

	// Stream 1 carries the explicit colorspace override
	olive::VideoStreamProperties props(
		reinterpret_cast<OakEngineNode *>(footage), 1);

	const auto combos = plain_combos(&props);
	ASSERT_EQ(combos.size(), 2);

	QComboBox *colorspace = combos.at(0);
	EXPECT_EQ(colorspace->currentText(), spaces.first());
	EXPECT_GT(colorspace->currentIndex(), 0);
}

TEST(DialogVideoStreamProperties, AcceptAppliesColorRangeChange)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;

	TestableFootage *footage = make_two_stream_footage(&project, QString());

	olive::VideoStreamProperties props(
		reinterpret_cast<OakEngineNode *>(footage), 0);

	const auto combos = plain_combos(&props);
	ASSERT_EQ(combos.size(), 2);
	QComboBox *range = combos.at(1);

	auto get_color_range = [&]() {
		OakEngineFootage *handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(footage));
		EXPECT_NE(handle, nullptr);
		int color_range = -1;
		oakengine_footage_get_video_stream_overrides(handle, 0, nullptr, 0,
													 &color_range, nullptr,
													 nullptr);
		oakengine_footage_free(handle);
		return color_range;
	};

	// Accepting without edits must not touch the stream
	props.accept(nullptr);
	EXPECT_EQ(get_color_range(), olive::VideoParams::k_color_range_limited);

	// Switching to full range is written back through the facade
	range->setCurrentIndex(1);
	props.accept(nullptr);
	EXPECT_EQ(get_color_range(), olive::VideoParams::k_color_range_full);
}
