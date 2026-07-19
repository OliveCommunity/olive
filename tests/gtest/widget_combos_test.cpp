#include <gtest/gtest.h>

#include <QSignalSpy>

#include "node/factory.h"
#include "render/videoparams.h"
#include "ui/colorcoding.h"
#include "widget/colorlabelmenu/colorcodingcombobox.h"
#include "widget/nodecombobox/nodecombobox.h"
#include "widget/standardcombos/standardcombos.h"

TEST(WidgetCombos, SampleRateContainsSupportedRates)
{
	olive::SampleRateComboBox combo;

	EXPECT_EQ(combo.count(),
			  int(olive::AudioParams::k_supported_sample_rates.size()));

	for (int rate : olive::AudioParams::k_supported_sample_rates) {
		combo.set_sample_rate(rate);
		EXPECT_EQ(combo.get_sample_rate(), rate);
	}
}

TEST(WidgetCombos, ChannelLayoutRoundTrips)
{
	olive::ChannelLayoutComboBox combo;

	EXPECT_EQ(combo.count(),
			  int(olive::AudioParams::k_supported_channel_layouts.size()));

	for (uint64_t layout : olive::AudioParams::k_supported_channel_layouts) {
		combo.set_channel_layout(layout);
		EXPECT_EQ(combo.get_channel_layout(), layout);
	}
}

TEST(WidgetCombos, InterlacedIndexesMatchEnum)
{
	olive::InterlacedComboBox combo;

	ASSERT_EQ(combo.count(), 3);

	combo.set_interlace_mode(olive::VideoParams::k_interlace_none);
	EXPECT_EQ(combo.get_interlace_mode(), olive::VideoParams::k_interlace_none);
	EXPECT_EQ(combo.currentIndex(), int(olive::VideoParams::k_interlace_none));

	combo.set_interlace_mode(olive::VideoParams::k_interlaced_top_first);
	EXPECT_EQ(combo.get_interlace_mode(), olive::VideoParams::k_interlaced_top_first);
	EXPECT_EQ(combo.currentIndex(), int(olive::VideoParams::k_interlaced_top_first));

	combo.set_interlace_mode(olive::VideoParams::k_interlaced_bottom_first);
	EXPECT_EQ(combo.get_interlace_mode(),
			  olive::VideoParams::k_interlaced_bottom_first);
	EXPECT_EQ(combo.currentIndex(),
			  int(olive::VideoParams::k_interlaced_bottom_first));
}

TEST(WidgetCombos, PixelFormatAllFormatsPresent)
{
	olive::PixelFormatComboBox combo(false);

	EXPECT_EQ(combo.count(), int(olive::core::PixelFormat::count));

	combo.set_pixel_format(olive::core::PixelFormat::f32);
	EXPECT_EQ(static_cast<olive::core::PixelFormat::Format>(combo.get_pixel_format()),
			  olive::core::PixelFormat::f32);

	combo.set_pixel_format(olive::core::PixelFormat::u8);
	EXPECT_EQ(static_cast<olive::core::PixelFormat::Format>(combo.get_pixel_format()),
			  olive::core::PixelFormat::u8);
}

TEST(WidgetCombos, PixelFormatFloatOnlyFilters)
{
	olive::PixelFormatComboBox combo(true);

	EXPECT_GT(combo.count(), 0);
	EXPECT_LT(combo.count(), int(olive::core::PixelFormat::count));

	for (int i = 0; i < combo.count(); i++) {
		olive::core::PixelFormat fmt =
			static_cast<olive::core::PixelFormat::Format>(
				combo.itemData(i).toInt());
		EXPECT_TRUE(fmt.is_float());
	}
}

TEST(WidgetCombos, VideoDividerRoundTrips)
{
	olive::VideoDividerComboBox combo;

	EXPECT_EQ(combo.count(), olive::VideoParams::k_supported_dividers.size());

	for (int d : olive::VideoParams::k_supported_dividers) {
		combo.set_divider(d);
		EXPECT_EQ(combo.get_divider(), d);
	}
}

TEST(WidgetCombos, FrameRateStandardAndCustom)
{
	olive::FrameRateComboBox combo;

	// Defaults to the first standard rate
	EXPECT_EQ(combo.get_frame_rate(),
			  olive::VideoParams::k_supported_frame_rates.first());

	// Selecting a standard rate just looks it up in the list
	const olive::Rational standard =
		olive::VideoParams::k_supported_frame_rates.at(2);
	combo.set_frame_rate(standard);
	EXPECT_EQ(combo.get_frame_rate(), standard);

	// A non-standard rate becomes the custom entry
	const olive::Rational custom(27, 2);
	combo.set_frame_rate(custom);
	EXPECT_EQ(combo.get_frame_rate(), custom);

	// Switching back to a standard rate works again
	combo.set_frame_rate(standard);
	EXPECT_EQ(combo.get_frame_rate(), standard);
}

TEST(WidgetCombos, PixelAspectRatioStandardAndCustom)
{
	olive::PixelAspectRatioComboBox combo;

	const QVector<olive::Rational> &standards =
		olive::VideoParams::k_standard_pixel_aspects;
	ASSERT_GE(standards.size(), 2);

	combo.set_pixel_aspect_ratio(standards.at(1));
	EXPECT_EQ(combo.get_pixel_aspect_ratio(), standards.at(1));

	// An unknown ratio lands on the last "Custom" item
	const olive::Rational custom(17, 13);
	combo.set_pixel_aspect_ratio(custom);
	EXPECT_EQ(combo.get_pixel_aspect_ratio(), custom);
	EXPECT_EQ(combo.currentIndex(), combo.count() - 1);
}

TEST(WidgetCombos, SampleFormatPackedFormatsRoundTrip)
{
	using Format = olive::core::SampleFormat::Format;

	olive::SampleFormatComboBox combo;
	combo.set_packed_formats();

	EXPECT_EQ(combo.count(),
			  int(olive::core::SampleFormat::packed_end) -
				  int(olive::core::SampleFormat::packed_start));

	for (int i = olive::core::SampleFormat::packed_start;
		 i < olive::core::SampleFormat::packed_end; i++) {
		const Format fmt = static_cast<Format>(i);
		combo.set_sample_format(fmt);
		EXPECT_EQ(static_cast<Format>(combo.get_sample_format()), fmt);
	}

	// Re-populating with restore enabled (the default) keeps the selection
	combo.set_sample_format(olive::core::SampleFormat::f32);
	combo.set_packed_formats();
	EXPECT_EQ(static_cast<Format>(combo.get_sample_format()),
			  olive::core::SampleFormat::f32);

	// Requesting a format that isn't in the list leaves the selection alone
	combo.set_sample_format(olive::core::SampleFormat::f32_p);
	EXPECT_EQ(static_cast<Format>(combo.get_sample_format()),
			  olive::core::SampleFormat::f32);
}

TEST(WidgetCombos, NodeComboBoxTracksSelectionWithoutSignal)
{
	olive::NodeFactory::initialize();

	{
		olive::NodeComboBox combo;
		QSignalSpy spy(&combo, &olive::NodeComboBox::node_changed);

		const QString id = QStringLiteral("org.olivevideoeditor.Olive.math");
		combo.set_node(id);
		EXPECT_EQ(combo.get_selected_node(), id);
		EXPECT_EQ(combo.count(), 1);
		EXPECT_EQ(combo.itemText(0), olive::NodeFactory::get_name_from_id(id));
		EXPECT_FALSE(combo.itemText(0).isEmpty());

		// Programmatic SetNode never emits NodeChanged
		EXPECT_EQ(spy.count(), 0);

		// Setting the same ID again is a no-op
		combo.set_node(id);
		EXPECT_EQ(combo.count(), 1);
		EXPECT_EQ(spy.count(), 0);

		// Clearing the selection empties the list
		combo.set_node(QString());
		EXPECT_TRUE(combo.get_selected_node().isEmpty());
		EXPECT_EQ(combo.count(), 0);
	}

	olive::NodeFactory::destroy();
}

TEST(WidgetCombos, ColorCodingComboSetColor)
{
	olive::ColorCodingComboBox combo;

	EXPECT_EQ(combo.get_selected_color(), 0);
	EXPECT_EQ(combo.count(), 1);
	EXPECT_EQ(combo.itemText(0), olive::ColorCoding::get_color_name(0));

	combo.set_color(3);
	EXPECT_EQ(combo.get_selected_color(), 3);
	EXPECT_EQ(combo.count(), 1);
	EXPECT_EQ(combo.itemText(0), olive::ColorCoding::get_color_name(3));
}
